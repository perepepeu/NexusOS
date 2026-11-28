#!/usr/bin/env python3
"""Validate Limine modules through VFS against the original host ELF files."""
from pathlib import Path
import importlib.util
import json
import re
import subprocess
import tempfile
import time

spec=importlib.util.spec_from_file_location("power_checks","scripts/check-power.py")
power=importlib.util.module_from_spec(spec); spec.loader.exec_module(power)
READY=b"Terminal pronto. Digite help."
report=Path("build/ramfs-results.json")
if report.exists(): report.unlink()
results=[]
def fingerprint(path):
    data=path.read_bytes(); value=2166136261
    for byte in data: value=((value^byte)*16777619)&0xffffffff
    return len(data),value
original={name:fingerprint(Path(f"build/user/{name}.elf")) for name in ("alpha","beta","fault","busy")}

def image(case):
    config=Path(f"build/limine-ramfs-{case}.conf")
    text=Path("boot/limine.conf").read_text()+"\n    cmdline: selftest=bootfs"+(" selftest=boot-no-modules" if case=="absent" else "")+"\n"
    if case=="mixed":
        for file,name in (("alpha","../escape"),("alpha","alpha"),("alpha","assets.bin"),("beta",None)):
            text+=f"    module_path: boot():/boot/programs/{file}.elf\n"
            if name is not None: text+=f"    module_string: {name}\n"
    config.write_text(text); iso=Path(f"build/nexusos-ramfs-{case}.iso")
    built=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if built.returncode: raise RuntimeError(built.stdout.decode(errors="replace"))
    return iso

def run(case,mode,iso):
    expected={} if case=="absent" else dict(original)
    if case=="mixed": expected.update({"assets.bin":original["alpha"],"beta.elf":original["beta"]})
    log=Path(f"build/ramfs-{case}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-ramfs-") as directory:
        sock=Path(directory)/"qmp.sock"
        command=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","128M","-boot","d","-net","none",
                 "-cdrom",str(iso),"-display","none","-serial","stdio","-monitor","none","-no-reboot",
                 "-qmp",f"unix:{sock},server=on,wait=off"]
        if mode=="uefi": command += ["-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        with log.open("wb") as output,subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def clean(start=0): return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes()[start:])
                def wait(predicate,label,timeout=40):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or b"KERNEL PANIC" in clean() or time.monotonic()>end:
                            raise RuntimeError(f"{case}/{mode}: {label}\n"+clean()[-6000:].decode(errors="replace"))
                        time.sleep(.03)
                wait(sock.exists,"QMP"); monitor=power.Monitor(str(sock)); wait(lambda:READY in clean(),"terminal")
                found={name.decode():(int(size),int(value,16)) for name,size,value in
                       re.findall(rb"BOOTFS FILE ([a-zA-Z0-9_.-]+) bytes=(\d+) hash=([0-9a-f]+)",clean())}
                assert found==expected,(found,expected)
                assert f"BOOTFS SELFTEST PASS: arquivos={len(expected)},".encode() in clean()
                assert f"/boot/modules: {len(expected)} modulo(s), {2 if case=='mixed' else 0} rejeitado(s)".encode() in clean()
                def type_command(text):
                    keys={" ":"spc","/":"slash",".":"dot","-":"minus"}
                    for char in text+"\n":
                        key="ret" if char=="\n" else keys.get(char,char)
                        monitor.execute("input-send-event",{"events":[
                            {"type":"key","data":{"down":down,"key":{"type":"qcode","data":key}}} for down in (True,False)]})
                        time.sleep(.03)
                def cmd(text,expected):
                    start=len(log.read_bytes()); type_command(text); wait(lambda:expected in clean(start),text)
                start=len(log.read_bytes()); type_command("ls /boot/modules"); type_command("echo ramfs-listed")
                wait(lambda:b"\r\nramfs-listed\r\n" in clean(start),"ls")
                for name in expected: assert (name+"\r\n").encode() in clean(start)
                cmd("write /boot/modules/alpha changed",b"VFS: somente leitura.")
                cmd("mkdir /boot/modules/newdir",b"VFS: somente leitura.")
                cmd("mkdir /tmp/ramfs",b"\r\n\rnexus> ")
                cmd("write /tmp/ramfs/probe ramfs-ok",b"\r\n\rnexus> ")
                cmd("cat /tmp/ramfs/probe",b"\r\nramfs-ok\r\n")
                cmd("vfsinfo",b"VFS: volumes=3 processos=1 descritores=0")
                assert clean().count(READY)==1
                type_command("shutdown"); process.wait(timeout=15); assert process.returncode==0
                results.append({"case":case,"mode":mode,"files":len(expected),"passed":True})
                print(f"OK: RAMFS {case}/{mode}: {len(expected)} arquivo(s), bytes/hash, readonly e RAM gravavel",flush=True)
            finally:
                if monitor: monitor.close()
                if process.poll() is None: process.terminate(); process.wait(timeout=10)
for case in ("normal","absent","mixed"):
    iso=image(case)
    for mode in ("bios","uefi"): run(case,mode,iso)
report.write_text(json.dumps(results,indent=2)+"\n")
print("OK: RAMFS e modulos Limine no VFS, BIOS/UEFI, ausencia, duplicatas e nomes invalidos.")
