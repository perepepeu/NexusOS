#!/usr/bin/env python3
"""VFS integration checks: process teardown, preemption and terminal commands."""
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
report=Path("build/vfs-results.json")
if report.exists(): report.unlink()
results=[]

def image(option):
    config=Path(f"build/limine-vfs-{option or 'terminal'}.conf")
    config.write_text(Path("boot/limine.conf").read_text()+(f"\n    cmdline: selftest={option}\n" if option else ""))
    iso=Path(f"build/nexusos-vfs-{option or 'terminal'}.iso")
    result=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError(result.stdout.decode(errors="replace"))
    return iso

def run(mode,ram,iso,test):
    label="core" if test else "terminal"; log=Path(f"build/vfs-{label}-{ram}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-vfs-") as directory:
        sock=Path(directory)/"qmp.sock"
        cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m",f"{ram}M","-boot","d","-net","none",
             "-cdrom",str(iso),"-display","none","-serial","stdio","-monitor","none","-no-reboot",
             "-qmp",f"unix:{sock},server=on,wait=off"]
        if mode=="uefi": cmd += ["-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        with log.open("wb") as output,subprocess.Popen(cmd,stdout=output,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def clean(): return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes())
                def wait(predicate,description,timeout=45):
                    end=time.monotonic()+timeout
                    while not predicate():
                        text=log.read_bytes()
                        if process.poll() is not None or b"KERNEL PANIC" in text or time.monotonic()>end:
                            raise RuntimeError(f"{label}/{mode}/{ram}: {description}\n"+text[-6500:].decode(errors="replace"))
                        time.sleep(.03)
                wait(sock.exists,"QMP"); monitor=power.Monitor(str(sock)); wait(lambda:READY in clean(),"terminal")
                assert b"VFS pronto:" in clean()
                if test:
                    text=clean()
                    assert b"VFS SELFTEST PASS" in text and b"sem FDs, referencias ou paginas vazadas" in text
                    assert b"PIT durante callback; preempcao adiada: OK" in text
                def type_command(text):
                    keys={" ":"spc","-":"minus","/":"slash",".":"dot"}
                    for char in text+"\n":
                        key="ret" if char=="\n" else keys.get(char,char)
                        monitor.execute("input-send-event",{"events":[
                            {"type":"key","data":{"down":down,"key":{"type":"qcode","data":key}}}
                            for down in (True,False)]})
                        time.sleep(.03)
                def command(text,expected):
                    previous=len(log.read_bytes()); type_command(text)
                    if expected==b"nexus> ": expected=b"\r\n\rnexus> "
                    wait(lambda:expected in re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes()[previous:]),text)
                command("vfsinfo",b"VFS: volumes=2 processos=1 descritores=0")
                if not test:
                    command("pwd",b"\r\n/\r\n")
                    command("cat /etc/motd",b"NexusOS: VFS e RAMFS prontos.\r\n")
                    command("mkdir /tmp/docs",b"nexus> ")
                    command("cd /tmp/docs",b"nexus> ")
                    command("pwd",b"\r\n/tmp/docs\r\n")
                    command("write note primeira",b"nexus> ")
                    command("append ./note segunda",b"nexus> ")
                    command("cat /tmp/docs/../docs/note",b"primeira\r\nsegunda\r\n")
                    command("ls .",b"\r\nnote\r\n")
                    command("cd note/..",b"VFS: nao e diretorio.")
                    command("umount /tmp",b"VFS: volume ocupado.")
                    command("cd /",b"nexus> ")
                    command("mount /mnt",b"Volume RAMFS montado.")
                    command("write /mnt/kept preservado",b"nexus> ")
                    command("cd /mnt",b"nexus> ")
                    command("umount .",b"VFS: volume ocupado.")
                    command("cd ..",b"nexus> ")
                    command("pwd",b"\r\n/\r\n")
                    command("umount /mnt",b"Volume desmontado com seguranca.")
                    command("cat /mnt/kept",b"VFS: nao encontrado.")
                    command("mount /mnt",b"Volume RAMFS montado.")
                    command("cat /mnt/kept",b"preservado\r\n")
                    command("umount /mnt",b"Volume desmontado com seguranca.")
                    command("vfsinfo",b"VFS: volumes=2 processos=1 descritores=0")
                command("echo vfs-terminal-ok",b"\r\nvfs-terminal-ok\r\n")
                assert clean().count(READY)==1 and b"KERNEL PANIC" not in clean()
                monitor.type_command("shutdown"); process.wait(timeout=15); assert process.returncode==0
                results.append({"case":label,"mode":mode,"ram_mib":ram,"passed":True})
                print(f"OK: VFS {label}/{mode}/{ram} MiB",flush=True)
            finally:
                if monitor: monitor.close()
                if process.poll() is None: process.terminate(); process.wait(timeout=10)

core=image("vfs"); normal=image("")
for mode in ("bios","uefi"):
    for ram in (128,512): run(mode,ram,core,True)
    run(mode,128,normal,False)
report.write_text(json.dumps(results,indent=2)+"\n")
print("OK: VFS BIOS/UEFI, paths, mounts, FDs/PID, exit/kill, threads, offsets e concorrencia preemptiva.")
