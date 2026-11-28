#!/usr/bin/env python3
"""Only new temporary mtools FAT32 images; validate output independently."""
from pathlib import Path
import importlib.util
import json
import re
import shutil
import struct
import subprocess
import tempfile
import time

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path); value=importlib.util.module_from_spec(spec); spec.loader.exec_module(value); return value
power=module("power_checks","scripts/check-power.py"); fixture=module("fat_fixture","scripts/fat32-fixture.py")
FSCK=shutil.which("fsck.fat") or "/usr/sbin/fsck.fat"
for tool in ("mformat","mcopy","mtype","mdir"):
    if not shutil.which(tool): raise SystemExit("Instale mtools para executar estes testes.")
if not Path(FSCK).is_file(): raise SystemExit("Instale dosfstools para executar fsck.fat.")
report=Path("build/fat32-write-results.json")
if report.exists(): report.unlink()
results=[]; READY=b"Terminal pronto. Digite help."

def check(disk,label,dirty=False):
    result=subprocess.run([FSCK,"-n","-v",str(disk)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    Path(f"build/fat32-fsck-{label}.log").write_bytes(result.stdout)
    if dirty:
        assert b"Dirty bit is set" in result.stdout,result.stdout.decode(errors="replace")
        assert not any(s in result.stdout for s in (b"Reclaimed",b"Start cluster beyond",b"contains a free cluster",b"FATs differ")),result.stdout.decode(errors="replace")
    else: assert not result.returncode,result.stdout.decode(errors="replace")

def copy(disk,work,name,contents):
    source=work/"extra.bin"; source.write_bytes(contents)
    subprocess.run(["mcopy","-o","-i",str(disk),str(source),f"::/{name}"],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)

def make(disk,work,full=False):
    metadata=fixture.create(disk,work,corrupt_file=False)
    copy(disk,work,"NEXUS.TST",b"NEXUS-FAT32-DISPOSABLE-v1\n")
    copy(disk,work,"Delete Long Name.txt",b"delete-long")
    if full:
        copy(disk,work,"FULL.BIN",b""); copy(disk,work,"FILL.BIN",b"")
        raw=disk.read_bytes(); fat_start=metadata["reserved"]*512
        free=sum(struct.unpack_from("<I",raw,fat_start+c*4)[0]&0xfffffff==0 for c in range(2,metadata["clusters"]+2))
        source=work/"fill.bin"
        with source.open("xb") as f: f.truncate(free*512)
        subprocess.run(["mcopy","-o","-i",str(disk),str(source),"::/FILL.BIN"],check=True,stdout=subprocess.PIPE,stderr=subprocess.PIPE)
    check(disk,"initial-full" if full else "initial")
    raw=disk.read_bytes(); fat_start=metadata["reserved"]*512
    metadata["first_free"]=next((c for c in range(2,metadata["clusters"]+2) if not (struct.unpack_from("<I",raw,fat_start+c*4)[0]&0xfffffff)),None)
    return metadata

def image(option):
    config=Path(f"build/limine-{option}.conf"); config.write_text(Path("boot/limine.conf").read_text()+f"\n    cmdline: selftest={option}\n")
    iso=Path(f"build/nexusos-{option}.iso")
    result=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError(result.stdout.decode(errors="replace"))
    return iso

def mtype(disk,path): return subprocess.check_output(["mtype","-i",str(disk),f"::/{path}"],stderr=subprocess.PIPE)
def verify(disk,metadata,label):
    expected=bytes((i*7+11)&255 for i in range(700))+bytes(600)+b"TAIL"
    assert mtype(disk,"B/RESULT.BIN")==expected
    assert mtype(disk,"B/SUB/NOTE.TXT")==b"moved-dir\n"
    assert mtype(disk,"RENAMED.TXT")==b"long-name-alias\nmore\n"
    assert mtype(disk,"FRAG.BIN")==bytes((i*7+11)&255 for i in range(8197))
    names=subprocess.check_output(["mdir","-i",str(disk),"-b","::/"],stderr=subprocess.PIPE)
    assert b"Long File Name Example" not in names and b"Delete Long Name" not in names and b"GROW" not in names
    raw=disk.read_bytes(); g=metadata
    assert raw[g["reserved"]*512:(g["reserved"]+g["fatsize"])*512]==raw[(g["reserved"]+g["fatsize"])*512:(g["reserved"]+2*g["fatsize"])*512]
    free=sum(not (struct.unpack_from("<I",raw,g["reserved"]*512+c*4)[0]&0xfffffff) for c in range(2,g["clusters"]+2))
    assert struct.unpack_from("<I",raw,512+488)[0]==free
    check(disk,label)

def run(case,mode,disk,iso,metadata):
    log=Path(f"build/fat32-write-{case}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-fat-write-qmp-") as tmp:
        root=Path(tmp); sock=root/"qmp.sock"; file=str(disk)
        if case=="error":
            config=root/"error.conf"; sector=metadata["data"]+metadata["first_free"]-2
            config.write_text(f'[inject-error]\nevent = "write_aio"\nsector = "{sector}"\nerrno = "5"\n')
            file=f"blkdebug:{config}:{disk}"
        cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","128M","-boot","d","-net","none","-cdrom",str(iso),
             "-display","none","-serial","stdio","-monitor","none","-no-reboot","-qmp",f"unix:{sock},server=on,wait=off",
             "-device","piix3-ide,id=storageide","-drive",f"file={file},if=none,id=disk,format=raw,cache=writeback",
             "-device","ide-hd,drive=disk,bus=storageide.0,unit=0,werror=report,rerror=report"]
        if mode=="uefi":
            variables=root/"vars.fd"; shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd",variables)
            cmd += ["-drive","if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd","-drive",f"if=pflash,format=raw,unit=1,file={variables}"]
        with log.open("wb") as out,subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def clean(start=0): return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes()[start:])
                def wait(predicate,label,timeout=100):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or b"KERNEL PANIC" in clean() or time.monotonic()>end:
                            raise RuntimeError(f"{case}/{mode}: {label}\n"+clean()[-6500:].decode(errors="replace"))
                        time.sleep(.03)
                wait(sock.exists,"QMP"); monitor=power.Monitor(str(sock)); wait(lambda:READY in clean(),"terminal")
                marker={"write":b"FAT32 WRITE PASS","persist":b"FAT32 PERSIST PASS","full":b"FAT32 FULL PASS","error":b"FAT32 WRITE ERROR PASS"}[case]
                assert marker in clean()
                def type_command(text):
                    keys={" ":"spc","/":"slash",".":"dot","-":"minus"}
                    for char in text.lower()+"\n":
                        key="ret" if char=="\n" else keys.get(char,char)
                        monitor.execute("input-send-event",{"events":[{"type":"key","data":{"down":d,"key":{"type":"qcode","data":key}}} for d in (True,False)]}); time.sleep(.03)
                def command(text,expected):
                    previous=len(log.read_bytes()); type_command(text); wait(lambda:expected in clean(previous),text)
                if case=="write":
                    command("mountfatrw ata0 /mnt",b"FAT32 montado para leitura e escrita")
                    command("mkdir /mnt/cli",b"\r\n\rnexus> ")
                    command("write /mnt/cli/note.txt cli-one",b"\r\n\rnexus> ")
                    command("append /mnt/cli/note.txt cli-two",b"\r\n\rnexus> ")
                    command("mv /mnt/cli/note.txt /mnt/cli/moved.txt",b"\r\n\rnexus> ")
                    command("truncate /mnt/cli/moved.txt 8",b"\r\n\rnexus> ")
                    command("cat /mnt/cli/moved.txt",b"\r\ncli-one\r\n")
                    command("rm /mnt/cli/moved.txt",b"\r\n\rnexus> "); command("rmdir /mnt/cli",b"\r\n\rnexus> ")
                    command("sync /mnt",b"Dados e metadados sincronizados.")
                    command("umount /mnt",b"Volume desmontado com seguranca.")
                command("echo fat-write-kernel-ok",b"\r\nfat-write-kernel-ok\r\n")
                assert clean().count(READY)==1
                type_command("shutdown"); process.wait(timeout=20); assert not process.returncode
                results.append({"case":case,"mode":mode,"passed":True})
                print(f"OK: FAT32 escrita {case}/{mode}",flush=True)
            finally:
                if monitor: monitor.close()
                if process.poll() is None: process.terminate(); process.wait(timeout=10)

images={case:image(option) for case,option in (("write","fat32-write"),("persist","fat32-persist"),("full","fat32-full"),("error","fat32-write-error"))}
with tempfile.TemporaryDirectory(prefix="nexus-fat32-write-disposable-",dir="/tmp") as tmp:
    root=Path(tmp)
    for mode in ("bios","uefi"):
        work=root/mode; work.mkdir(); disk=work/"volume.raw"; metadata=make(disk,work)
        run("write",mode,disk,images["write"],metadata); verify(disk,metadata,f"write-{mode}")
        run("persist",mode,disk,images["persist"],metadata); verify(disk,metadata,f"persist-{mode}")
        full_work=work/"full"; full_work.mkdir(); full=full_work/"volume.raw"; fm=make(full,full_work,True)
        run("full",mode,full,images["full"],fm); check(full,f"full-{mode}")
        assert mtype(full,"FULL.BIN")==b"" and mtype(full,"FILL.BIN").startswith(b"full-ok")
        error_work=work/"error"; error_work.mkdir(); damaged=error_work/"volume.raw"; em=make(damaged,error_work)
        run("error",mode,damaged,images["error"],em); check(damaged,f"error-{mode}",dirty=True)
report.write_text(json.dumps(results,indent=2)+"\n")
print("OK: FAT32 escrita/persistencia/cheio/falha, BIOS/UEFI, mtools e fsck.fat.")
