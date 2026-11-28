#!/usr/bin/env python3
from pathlib import Path
import importlib.util,json,subprocess,tempfile,time
spec=importlib.util.spec_from_file_location("power_checks","scripts/check-power.py"); power=importlib.util.module_from_spec(spec); spec.loader.exec_module(power)
config=Path("build/limine-fs-syscalls.conf")
config.write_text(Path("boot/limine.conf").read_text()+"\n    module_path: boot():/boot/programs/fsio.elf\n    module_string: fsio\n    cmdline: selftest=fs-syscalls\n")
iso=Path("build/nexusos-fs-syscalls.iso")
r=subprocess.run(["bash","scripts/build-iso.sh",str(config),str(iso)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT)
if r.returncode: raise RuntimeError(r.stdout.decode(errors="replace"))
report=Path("build/fs-syscalls-results.json")
if report.exists():report.unlink()
results=[]
for mode in ("bios","uefi"):
    log=Path(f"build/fs-syscalls-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-fs-syscalls-") as directory:
        sock=Path(directory)/"qmp.sock"
        cmd=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","128M","-boot","d","-net","none","-cdrom",str(iso),
             "-display","none","-serial","stdio","-monitor","none","-no-reboot","-qmp",f"unix:{sock},server=on,wait=off"]
        if mode=="uefi":cmd += ["-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        with log.open("wb") as output,subprocess.Popen(cmd,stdout=output,stderr=subprocess.STDOUT) as process:
            monitor=None
            try:
                def wait(predicate,label,timeout=40):
                    end=time.monotonic()+timeout
                    while not predicate():
                        if process.poll() is not None or b"KERNEL PANIC" in log.read_bytes() or time.monotonic()>end:
                            raise RuntimeError(f"{mode}: {label}\n"+log.read_text(errors="replace")[-5000:])
                        time.sleep(.03)
                wait(sock.exists,"QMP");monitor=power.Monitor(str(sock));wait(lambda:b"Terminal pronto. Digite help." in log.read_bytes(),"terminal")
                text=log.read_bytes(); assert b"FS SYSCALL PASS" in text and text.count(b"FSIO USER PASS")==2
                monitor.type_command("shutdown");process.wait(timeout=15);assert not process.returncode
                results.append({"mode":mode,"passed":True});print(f"OK: syscalls VFS Ring3 {mode}",flush=True)
            finally:
                if monitor:monitor.close()
                if process.poll() is None:process.terminate();process.wait(timeout=10)
report.write_text(json.dumps(results,indent=2)+"\n")
