#!/usr/bin/env python3
"""ATA PIO integration checks. ALL writable disks are new temporary files.

Sparse high-LBA images live in /tmp (WSL ext4), not on a Windows-mounted drive.
No existing image or host disk can be supplied to this script.
"""
from pathlib import Path
import importlib.util
import json
import os
import subprocess
import tempfile
import time

spec=importlib.util.spec_from_file_location("power_checks", "scripts/check-power.py")
power=importlib.util.module_from_spec(spec); spec.loader.exec_module(power)
SIGNATURE=b"NEXUS-ATA-TEST-DISK-v1\n"
READY=b"Terminal pronto. Digite help."
results=[]
report=Path("build/storage-results.json")
if report.exists(): report.unlink()

def pattern(lba, written=False):
    return bytes((lba*17+i*3+(0xa7 if written else 0x31))&255 for i in range(512))

def contents(sectors, written):
    data={0:SIGNATURE+bytes(512-len(SIGNATURE))}
    for lba in (2,3,4): data[lba]=pattern(lba)
    if written:
        for lba in (8,9,10,sectors-1): data[lba]=pattern(lba,True)
        if sectors>(1<<28)+9:
            for lba in ((1<<28)+7,(1<<28)+8): data[lba]=pattern(lba,True)
    return data

def make_disk(path,sectors,signature=True):
    with path.open("xb") as disk:
        disk.truncate(sectors*512)
        for lba,data in contents(sectors,False).items():
            disk.seek(lba*512); disk.write(data if signature or lba else bytes(512))

def verify_disk(path,sectors,written):
    expected=contents(sectors,written)
    with path.open("rb") as disk:
        for lba,data in expected.items():
            disk.seek(lba*512); assert disk.read(512)==data,(path,lba)
        # Check every allocated extent, including guard sectors. Skip sparse holes.
        offset=0
        while offset<sectors*512:
            try: start=os.lseek(disk.fileno(),offset,os.SEEK_DATA)
            except OSError as error:
                if error.errno==6: break  # ENXIO: no more allocated extents
                raise
            end=min(os.lseek(disk.fileno(),start,os.SEEK_HOLE),sectors*512)
            disk.seek(start)
            while start<end:
                actual=disk.read(min(1024*1024,end-start))
                wanted=bytearray(len(actual))
                for lba,data in expected.items():
                    position=lba*512-start
                    if 0<=position<=len(wanted)-512: wanted[position:position+512]=data
                assert actual==wanted,f"Escrita fora das regioes previstas: {path} offset={start}"
                start+=len(actual)
            offset=end

def image(option):
    config=Path(f"build/limine-storage-{option or 'normal'}.conf")
    config.write_text(Path("boot/limine.conf").read_text()+(f"\n    cmdline: selftest={option}\n" if option else ""))
    iso=Path(f"build/nexusos-storage-{option or 'normal'}.iso")
    built=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
    if built.returncode: raise RuntimeError(built.stdout.decode(errors="replace"))
    return iso

def run(label,iso,disks=(),mode="bios",option="",packet=False,refuse=False,fault=None):
    log=Path(f"build/storage-{label}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-ata-qmp-") as tmp:
        sock=Path(tmp)/"qmp.sock"
        cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","256M","-cdrom",str(iso),"-boot","d",
             "-display","none","-serial","stdio","-monitor","none","-net","none","-no-reboot",
             "-qmp",f"unix:{sock},server=on,wait=off"]
        if disks or packet: cmd += ["-device","piix3-ide,id=storageide"]
        for index,(path,bus,unit) in enumerate(disks):
            file=f"blkdebug:{fault}:{path}" if fault else str(path)
            cmd += ["-drive",f"file={file},if=none,id=d{index},format=raw,cache=writeback",
                    "-device",f"ide-hd,drive=d{index},bus=storageide.{bus},unit={unit},rerror=report,werror=report"]
        if packet:
            cmd += ["-drive",f"file={iso},if=none,id=packet,format=raw,media=cdrom,readonly=on",
                    "-device","ide-cd,drive=packet,bus=storageide.1,unit=1"]
        if mode=="uefi": cmd += ["-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        with log.open("wb") as output,subprocess.Popen(cmd,stdout=output,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def wait(predicate,timeout=45):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or time.monotonic()>end:
                            raise RuntimeError(f"Timeout/saida em {label}:\n"+log.read_text(errors="replace")[-6000:])
                        time.sleep(.05)
                wait(sock.exists); monitor=power.Monitor(str(sock))
                if refuse:
                    wait(lambda:b"Sistema interrompido. Reinicie a maquina." in log.read_bytes())
                    assert b"Disco sem assinatura descartavel; escrita recusada." in log.read_bytes()
                    assert b"STORAGE WRITE PASS" not in log.read_bytes()
                else:
                    wait(lambda:READY in log.read_bytes())
                    text=log.read_bytes()
                    assert b"KERNEL PANIC" not in text
                    assert f"{len(disks)} dispositivo(s) de bloco disponivel(is).".encode() in text
                    if fault: assert b"STORAGE ERROR PASS" in text
                    elif option:
                        marker=b"STORAGE VERIFY PASS" if option=="storage-verify" else b"STORAGE WRITE PASS"
                        assert marker in text
                        assert text.count(b"leitura, escrita, flush e limites OK")==len(disks)
                    if packet: assert b"nao suportado" in text
                    monitor.type_command("disks")
                    wait(lambda:b"|" in log.read_bytes() if disks else b"Nenhum disco ATA PIO" in log.read_bytes())
                    monitor.type_command("echo storage-terminal-ok")
                    wait(lambda:b"\r\nstorage-terminal-ok\r\n" in log.read_bytes())
                    monitor.type_command("shutdown"); process.wait(timeout=15)
                    assert process.returncode==0
                results.append({"case":label,"mode":mode,"disks":len(disks),"passed":True})
                print(f"OK: {label} ({mode}), {len(disks)} disco(s)",flush=True)
            finally:
                if monitor: monitor.close()
                if process.poll() is None: process.terminate(); process.wait(timeout=10)

normal=image(""); write=image("storage"); verify=image("storage-verify")
with tempfile.TemporaryDirectory(prefix="nexus-ata-disposable-",dir="/tmp") as directory:
    root=Path(directory)
    for mode in ("bios","uefi"):
        run("ahci-only",normal,mode=mode)
        disks=[]
        geometry=(32768,49152,(1<<28)+4096)
        for index,sectors in enumerate(geometry):
            path=root/f"{mode}-{index}.raw"; make_disk(path,sectors)
            disks.append((path,0 if index<2 else 1,index%2))
        run("write",write,disks,mode,option="storage",packet=True)
        for (path,_,_),sectors in zip(disks,geometry): verify_disk(path,sectors,True)
        run("persist",verify,disks,mode,option="storage-verify",packet=True)
        for (path,_,_),sectors in zip(disks,geometry): verify_disk(path,sectors,True)
        for path,_,_ in disks: path.unlink()
    unmarked=root/"unsigned.raw"; make_disk(unmarked,32768,False)
    run("refuse-unsigned",write,[(unmarked,0,0)],refuse=True)
    with unmarked.open("rb") as disk:
        for lba in (0,8,9,10,32767): disk.seek(lba*512); assert disk.read(512)==bytes(512)
    for operation,event,sector in (("read","read_aio",2),("write","write_aio",8),("flush","flush_to_disk",None)):
        disk=root/f"error-{operation}.raw"; make_disk(disk,32768)
        config=root/f"error-{operation}.conf"
        config.write_text(f'[inject-error]\nevent = "{event}"\nerrno = "5"\n'+
                          (f'sector = "{sector}"\n' if sector is not None else ''))
        option=f"storage-error-{operation}"
        run(f"error-{operation}",image(option),[(disk,0,0)],option=option,fault=config)
report.write_text(json.dumps(results,indent=2)+"\n")
print("OK: ATA no QEMU BIOS/UEFI, master/slave, canais primario/secundario, LBA > 28 bits, flush e persistencia.")
