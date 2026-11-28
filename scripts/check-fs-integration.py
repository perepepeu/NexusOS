#!/usr/bin/env python3
"""End-to-end disk ELF/syscalls/tools and real interrupted-write recovery.
Every mutable image and pflash NVRAM is newly created in a temporary directory.
"""
from pathlib import Path
import importlib.util,json,re,shutil,struct,subprocess,tempfile,time,hashlib
def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path); value=importlib.util.module_from_spec(spec); spec.loader.exec_module(value); return value
power=module("power_checks","scripts/check-power.py"); fixture=module("fat_fixture","scripts/fat32-fixture.py")
FSCK=shutil.which("fsck.fat") or "/usr/sbin/fsck.fat"
if not Path(FSCK).exists():raise SystemExit("Instale dosfstools e mtools no WSL.")
report=Path("build/fs-integration-results.json")
if report.exists():report.unlink()
results=[]; images={}; READY=b"Terminal pronto. Digite help."

class Fat:
    def __init__(self,path):
        self.path=path; self.raw=bytearray(path.read_bytes()); b=self.raw
        self.reserved=struct.unpack_from("<H",b,14)[0]; self.fats=b[16]; self.sp=b[13]
        self.size=struct.unpack_from("<I",b,36)[0]; self.root=struct.unpack_from("<I",b,44)[0]
        self.data=self.reserved+self.fats*self.size; self.clusters=(len(b)//512-self.data)//self.sp
    def get(self,c):return struct.unpack_from("<I",self.raw,self.reserved*512+c*4)[0]&0xfffffff
    def put(self,c,value):
        for copy in range(self.fats):
            offset=(self.reserved+copy*self.size)*512+c*4; old=struct.unpack_from("<I",self.raw,offset)[0]
            struct.pack_into("<I",self.raw,offset,(old&0xf0000000)|value)
    def location(self,c):return (self.data+(c-2)*self.sp)*512
    def chain(self,first):
        seen=set(); out=[]
        while first<0xffffff8:
            assert 2<=first<self.clusters+2 and first not in seen
            seen.add(first);out.append(first);first=self.get(first)
        return out
    def entries(self,first):
        out={}
        for c in self.chain(first):
            for at in range(self.location(c),self.location(c)+self.sp*512,32):
                e=self.raw[at:at+32]
                if not e[0]:return out
                if e[0]==0xe5 or e[11]&8:continue
                name=e[:8].decode("ascii").rstrip();ext=e[8:11].decode("ascii").rstrip()
                if ext:name+="."+ext
                out[name]=(at,struct.unpack_from("<H",e,26)[0]|(struct.unpack_from("<H",e,20)[0]<<16))
        return out
    def fragment(self,name):
        first=self.entries(self.root)[name][1]; old=self.chain(first);assert len(old)>2
        free=next(c for c in range(5000,self.clusters+2) if not self.get(c))
        source=self.location(old[1]);target=self.location(free)
        self.raw[target:target+self.sp*512]=self.raw[source:source+self.sp*512]
        self.put(free,self.get(old[1]));self.put(old[0],free);self.put(old[1],0);self.path.write_bytes(self.raw)
        assert self.chain(first)[1]==free

def copy(disk,work,name,data):
    source=work/"source-extra.bin";source.write_bytes(data)
    subprocess.run(["mcopy","-o","-i",str(disk),str(source),f"::/{name}"],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
def make(disk,work):
    fixture.create(disk,work,corrupt_file=False)
    copy(disk,work,"NEXUS.TST",b"NEXUS-FAT32-DISPOSABLE-v1\n")
    for name,source in (("FSIO.ELF","fsio"),("HOLD.ELF","hold"),("FAULT.ELF","fsfault")):
        copy(disk,work,name,Path(f"build/user/{source}.elf").read_bytes())
    copy(disk,work,"BAD.ELF",b"not-an-elf")
    bad=bytearray(Path("build/user/fsio.elf").read_bytes());phoff=struct.unpack_from("<Q",bad,32)[0];struct.pack_into("<I",bad,phoff+4,7)
    copy(disk,work,"RWX.ELF",bad);copy(disk,work,"HUGE.ELF",bytes(1024*1024+1))
    Fat(disk).fragment("FSIO.ELF")
    assert mtype(disk,"FSIO.ELF")==Path("build/user/fsio.elf").read_bytes();fsck(disk,"initial")
def mtype(disk,path):return subprocess.check_output(["mtype","-i",str(disk),f"::/{path}"],stderr=subprocess.PIPE)
def fsck(disk,label,repair=False,dirty=False):
    r=subprocess.run([FSCK,"-a" if repair else "-n","-v",str(disk)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    Path(f"build/fs-integration-fsck-{label}.log").write_bytes(r.stdout)
    if repair:assert r.returncode in (0,1),r.stdout.decode(errors="replace")
    elif dirty:assert r.returncode==1 and b"Dirty bit is set" in r.stdout,r.stdout.decode(errors="replace")
    else:assert not r.returncode,r.stdout.decode(errors="replace")
def verify(disk,label):
    fat=Fat(disk); entries=fat.entries(fat.root); pids=[]
    for name,(_,cluster) in entries.items():
        if name.startswith("P") and name[1:].isdigit():
            pid=int(name[1:]);pids.append(pid);data=mtype(disk,f"{name}/DATA.BIN")
            assert data==bytes((pid*17+i*7)&255 for i in range(12288))
    assert len(pids)>=4,pids
    data=mtype(disk,"SHARED.BIN");assert len(data)>=2048 and len(data)%64==0
    records={}
    for at in range(0,len(data),64):
        pid,seq=struct.unpack_from("<QQ",data,at);assert pid in pids and seq<16
        assert seq not in records.setdefault(pid,set());records[pid].add(seq)
        assert data[at+16:at+64]==bytes((pid+seq+i)&255 for i in range(16,64))
    assert all(len(v)==16 for v in records.values())
    assert mtype(disk,"COPY.BIN")==bytes((i*7+11)&255 for i in range(8197))
    assert mtype(disk,"COPY2.BIN")==mtype(disk,"COPY.BIN") and mtype(disk,"TOUCH.TXT")==b""
    b=entries["FSIO.ELF"][1];assert len(fat.chain(b))>2 and fat.chain(b)[1]-b>1
    start=fat.reserved*512;end=start+fat.size*512
    assert fat.raw[start:end]==fat.raw[end:end+fat.size*512]
    fsck(disk,label)

def image(option):
    if option in images:return images[option]
    config=Path(f"build/limine-{option}.conf");config.write_text(Path("boot/limine.conf").read_text()+f"\n    cmdline: selftest={option}\n")
    iso=Path(f"build/nexusos-{option}.iso")
    r=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if r.returncode:raise RuntimeError(r.stdout.decode(errors="replace"))
    images[option]=iso;return iso

def run(case,mode,disk):
    log=Path(f"build/fs-integration-{case}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-fs-qmp-") as tmp:
        root=Path(tmp);sock=root/"qmp.sock";iso=image(case)
        cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","128M","-boot","d","-net","none","-cdrom",str(iso),
             "-display","none","-serial","stdio","-monitor","none","-no-reboot","-qmp",f"unix:{sock},server=on,wait=off",
             "-device","piix3-ide,id=storageide","-drive",f"file={disk},if=none,id=disk,format=raw,cache=writeback",
             "-device","ide-hd,drive=disk,bus=storageide.0,unit=0,werror=report,rerror=report"]
        if mode=="uefi":
            variables=root/"vars.fd";shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd",variables)
            cmd += ["-drive","if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd","-drive",f"if=pflash,format=raw,unit=1,file={variables}"]
        with log.open("wb") as out,subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def clean(start=0):return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes()[start:])
                def wait(predicate,label,timeout=150):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or b"KERNEL PANIC" in clean() or time.monotonic()>end:
                            raise RuntimeError(f"{case}/{mode}: {label}\n"+clean()[-8000:].decode(errors="replace"))
                        time.sleep(.03)
                wait(sock.exists,"QMP");monitor=power.Monitor(str(sock))
                if case=="fs-crash":
                    wait(lambda:b"FS CRASH READY" in clean(),"checkpoint");assert READY not in clean()
                    process.kill();process.wait(timeout=10)
                else:
                    wait(lambda:READY in clean(),"terminal")
                    marker={"fs-disk":b"FS DISK PASS","fs-persist":b"FS PERSIST PASS","fs-dirty":b"FS DIRTY PASS","fs-recover":b"FS RECOVERY PASS"}[case]
                    assert marker in clean() and b"Modulo pronto: fsio" not in clean()
                    def type_command(text):
                        keys={" ":"spc","/":"slash",".":"dot","-":"minus"}
                        for char in text.lower()+"\n":
                            key="ret" if char=="\n" else keys.get(char,char)
                            monitor.execute("input-send-event",{"events":[{"type":"key","data":{"down":d,"key":{"type":"qcode","data":key}}} for d in (True,False)]});time.sleep(.03)
                    def command(text,expected):
                        previous=len(log.read_bytes());type_command(text);wait(lambda:expected in clean(previous),text)
                    if case=="fs-disk":
                        assert clean().count(b"FSIO USER PASS")==4
                        command("mountfatrw ata0 /mnt",b"FAT32 montado para leitura e escrita")
                        command("mount",b"fat32 em /mnt (RW)")
                        command("df",b"/mnt: total=")
                        command("df /mnt",b"livres=")
                        command("mkdir /mnt/cli",b"\r\n\rnexus> ")
                        command("cd /mnt/cli",b"\r\n\rnexus> ");command("pwd",b"\r\n/mnt/CLI\r\n")
                        command("touch note.txt",b"\r\n\rnexus> ")
                        command("write note.txt command-ok",b"\r\n\rnexus> ")
                        command("append note.txt append-ok",b"\r\n\rnexus> ")
                        command("touch note.txt",b"\r\n\rnexus> ")
                        command("cat note.txt",b"command-ok\r\nappend-ok\r\n")
                        command("ls .",b"NOTE.TXT\r\n")
                        command("cp note.txt copy.txt",b"\r\n\rnexus> ")
                        command("mv copy.txt moved.txt",b"\r\n\rnexus> ")
                        command("rm moved.txt",b"\r\n\rnexus> ");command("rm note.txt",b"\r\n\rnexus> ")
                        command("cd /",b"\r\n\rnexus> ");command("rmdir /mnt/cli",b"\r\n\rnexus> ")
                        command("cp /mnt/frag.bin /mnt/copy2.bin",b"\r\n\rnexus> ")
                        command("cp /mnt/frag.bin /mnt/./frag.bin",b"VFS: argumento invalido.")
                        previous=len(log.read_bytes());type_command("run /mnt/fsio.elf")
                        wait(lambda:b"FSIO USER PASS" in clean(previous),"ELF pelo terminal")
                        match=re.search(rb"Processo ELF criado: (\d+) \(/mnt/fsio.elf\)",clean(previous));assert match
                        command("wait "+match[1].decode(),b"status=0")
                        command("vfsinfo",b"descritores=0")
                        command("sync",b"Dados e metadados sincronizados.")
                        command("umount /mnt",b"Volume desmontado com seguranca.")
                    command("echo fs-integration-ok",b"\r\nfs-integration-ok\r\n")
                    type_command("shutdown");process.wait(timeout=20);assert not process.returncode
                results.append({"case":case,"mode":mode,"passed":True});print(f"OK: {case}/{mode}",flush=True)
            finally:
                if monitor:monitor.close()
                if process.poll() is None:process.terminate();process.wait(timeout=10)

with tempfile.TemporaryDirectory(prefix="nexus-fs-disposable-",dir="/tmp") as tmp:
    root=Path(tmp)
    for mode in ("bios","uefi"):
        work=root/mode;work.mkdir();disk=work/"disk.raw";make(disk,work)
        run("fs-disk",mode,disk);verify(disk,f"disk-{mode}")
        before=hashlib.sha256(disk.read_bytes()).hexdigest();run("fs-persist",mode,disk)
        assert hashlib.sha256(disk.read_bytes()).hexdigest()==before;verify(disk,f"persist-{mode}")
        cut=work/"cut";cut.mkdir();damaged=cut/"disk.raw";make(damaged,cut)
        run("fs-crash",mode,damaged);fsck(damaged,f"cut-{mode}",dirty=True)
        run("fs-dirty",mode,damaged);fsck(damaged,f"still-dirty-{mode}",dirty=True)
        assert mtype(damaged,"FRAG.BIN")==bytes((i*7+11)&255 for i in range(8197))
        fsck(damaged,f"repair-{mode}",repair=True);fsck(damaged,f"repaired-{mode}")
        run("fs-recover",mode,damaged);fsck(damaged,f"recovered-{mode}")
        assert mtype(damaged,"RECOVER.TXT")==b""
report.write_text(json.dumps(results,indent=2)+"\n")
print("OK: syscalls/processos/ELF em disco, comandos, persistencia e corte/recuperacao BIOS/UEFI.")
