#!/usr/bin/env python3
from pathlib import Path
import hashlib
import importlib.util
import json
import shutil
import struct
import subprocess
import tempfile
import time
import re

def module(name,path):
    spec=importlib.util.spec_from_file_location(name,path); loaded=importlib.util.module_from_spec(spec); spec.loader.exec_module(loaded); return loaded
power=module("power_checks","scripts/check-power.py"); fixture=module("fat32_fixture","scripts/fat32-fixture.py")
for tool in ("mformat","mcopy","mmd","mtype"):
    if not shutil.which(tool): raise SystemExit("Instale mtools no WSL para executar check-fat32.")
report=Path("build/fat32-results.json")
if report.exists(): report.unlink()
results=[]; READY=b"Terminal pronto. Digite help."

def image(option):
    config=Path(f"build/limine-{option}.conf"); config.write_text(Path("boot/limine.conf").read_text()+f"\n    cmdline: selftest={option}\n")
    iso=Path(f"build/nexusos-{option}.iso")
    r=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if r.returncode: raise RuntimeError(r.stdout.decode(errors="replace"))
    return iso

def run(case,mode,disk,iso,metadata):
    before=hashlib.sha256(disk.read_bytes()).hexdigest(); log=Path(f"build/fat32-{case}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-fat32-qmp-") as temporary:
        sock=Path(temporary)/"qmp.sock"
        cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","128M","-boot","d","-net","none",
             "-cdrom",str(iso),"-display","none","-serial","stdio","-monitor","none","-no-reboot",
             "-qmp",f"unix:{sock},server=on,wait=off","-device","piix3-ide,id=storageide",
             "-drive",f"file={disk},if=none,id=disk,format=raw","-device",
             "ide-hd,drive=disk,bus=storageide.0,unit=0,werror=report"]
        if mode=="uefi":
            # Give OVMF dedicated NVRAM; CODE-only firmware can persist variables
            # to the writable FAT disk before the kernel even starts.
            variables=Path(temporary)/"OVMF_VARS.fd"
            shutil.copyfile("/usr/share/OVMF/OVMF_VARS_4M.fd",variables)
            cmd += ["-drive","if=pflash,format=raw,unit=0,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd",
                    "-drive",f"if=pflash,format=raw,unit=1,file={variables}"]
        with log.open("wb") as out,subprocess.Popen(cmd,stdout=out,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def clean(start=0): return re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]",b"",log.read_bytes()[start:])
                def wait(predicate,label,timeout=50):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or b"KERNEL PANIC" in clean() or time.monotonic()>end:
                            raise RuntimeError(f"{case}/{mode}: {label}\n"+clean()[-6500:].decode(errors="replace"))
                        time.sleep(.03)
                wait(sock.exists,"QMP"); monitor=power.Monitor(str(sock)); wait(lambda:READY in clean(),"terminal")
                def writes():
                    stats=next(item["stats"] for item in monitor.execute("query-blockstats") if item.get("device")=="disk")
                    return tuple(stats.get(key,0) for key in ("wr_bytes","wr_operations","failed_wr_operations"))
                initial_writes=writes()
                assert initial_writes==(0,0,0),f"Escritas antes do terminal: {initial_writes}"
                valid=case in ("normal","offset")
                if valid:
                    value=2166136261
                    for i in range(8197): value=((value^((i*7+11)&255))*16777619)&0xffffffff
                    assert f"FAT32 SELFTEST PASS bytes=8197 hash={value:x}".encode() in clean()
                else: assert b"FAT32 REJECT PASS" in clean()
                def type_command(text):
                    keys={" ":"spc","/":"slash",".":"dot","-":"minus"}
                    for char in text.lower()+"\n":
                        key="ret" if char=="\n" else keys.get(char,char)
                        monitor.execute("input-send-event",{"events":[{"type":"key","data":{"down":d,"key":{"type":"qcode","data":key}}} for d in (True,False)]}); time.sleep(.03)
                def command(text,expected):
                    start=len(log.read_bytes()); type_command(text); wait(lambda:expected in clean(start),text)
                mount="mountfat ata0 /mnt"+(" 2048 131072" if case=="offset" else "")
                command(mount,b"FAT32 montado somente para leitura" if valid else b"Montagem recusada")
                if valid:
                    command("cat /mnt/SUB/CHILD.TXT",b"fat32-child")
                    command("write /mnt/FRAG.BIN no",b"VFS: somente leitura.")
                    command("cat /mnt/BROKEN.BIN",b"VFS: erro de I/O.")
                    start=len(log.read_bytes()); type_command("ls /mnt"); type_command("echo fat-listed")
                    wait(lambda:b"\r\nfat-listed\r\n" in clean(start),"listagem")
                    for alias in metadata["aliases"]: assert alias.encode() in clean(start)
                    command("umount /mnt",b"Volume desmontado com seguranca.")
                command("echo fat32-kernel-ok",b"\r\nfat32-kernel-ok\r\n")
                assert clean().count(READY)==1
                assert writes()==initial_writes,"Operacao de leitura emitiu escrita apos o boot!"
                type_command("shutdown"); process.wait(timeout=15); assert not process.returncode
            finally:
                if monitor: monitor.close()
                if process.poll() is None: process.terminate(); process.wait(timeout=10)
    assert hashlib.sha256(disk.read_bytes()).hexdigest()==before,"Driver de leitura modificou a imagem!"
    results.append({"case":case,"mode":mode,"passed":True,"disk_unchanged":True})
    print(f"OK: FAT32 {case}/{mode}; imagem intacta",flush=True)

normal=image("fat32"); invalid=image("fat32-invalid"); offset=image("fat32-offset")
with tempfile.TemporaryDirectory(prefix="nexus-fat32-disposable-",dir="/tmp") as temporary:
    root=Path(temporary); disk=root/"volume.raw"; metadata=fixture.create(disk,root)
    for mode in ("bios","uefi"): run("normal",mode,disk,normal,metadata)
    for case in ("bpb","cycle"):
        damaged=root/f"{case}.raw"; shutil.copyfile(disk,damaged)
        with damaged.open("r+b") as f:
            if case=="bpb": f.seek(13); f.write(b"\x03")
            else:
                for fat in range(metadata["fats"]):
                    f.seek((metadata["reserved"]+fat*metadata["fatsize"])*512+metadata["root"]*4)
                    f.write(struct.pack("<I",metadata["root"]))
        for mode in ("bios","uefi"): run(case,mode,damaged,invalid,metadata)
    partitioned=root/"offset.raw"
    with partitioned.open("xb") as f:
        f.write(b"G"*(2048*512)); f.write(disk.read_bytes()); f.write(b"T"*(64*512))
    for mode in ("bios","uefi"): run("offset",mode,partitioned,offset,metadata)
report.write_text(json.dumps(results,indent=2)+"\n")
print("OK: FAT32 BIOS/UEFI, mtools, fragmentacao, LFN/8.3, corrupcao, limites de volume e ausencia de escritas.")
