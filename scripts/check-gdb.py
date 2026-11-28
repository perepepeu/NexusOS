"""Conecta GDB real ao QEMU, para na entrada C e verifica ABI/data/BSS."""
from pathlib import Path
import socket
import subprocess
import tempfile
import time
for mode in ("bios", "uefi"):
    with socket.socket() as probe:
        probe.bind(("127.0.0.1", 0)); port = probe.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="nexus-gdb-") as directory:
        script = Path(directory)/"check.gdb"
        script.write_text(f"""set pagination off
set confirm off
set architecture i386:x86-64
file build/kernel.elf
target remote 127.0.0.1:{port}
hbreak *kmain
continue
if (((unsigned long)$rsp & 15) != 8)
  echo STACK ABI INVALIDA\\n
  quit 1
end
if boot_data_probe != 0x4e455855534f5321
  quit 2
end
set $i = 0
while $i < 8
  if boot_bss_probe[$i] != 0
    quit 3
  end
  set $i = $i + 1
end
echo GDB ABI DATA BSS OK\\n
detach
quit 0
""")
        command=["qemu-system-x86_64","-machine","q35","-accel","tcg","-m","256M","-cdrom","build/nexusos.iso",
                 "-boot","d","-display","none","-serial","stdio","-monitor","none","-net","none","-no-reboot",
                 "-S","-gdb",f"tcp:127.0.0.1:{port}"]
        if mode=="uefi": command += ["-drive","if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        log=Path(f"build/gdb-{mode}-boot.log")
        with log.open("wb") as output, subprocess.Popen(command,stdout=output,stderr=subprocess.STDOUT) as process:
            try:
                deadline=time.monotonic()+10
                while True:
                    try:
                        with socket.create_connection(("127.0.0.1",port),timeout=0.2): break
                    except OSError:
                        if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError("GDB stub indisponivel")
                        time.sleep(0.05)
                result=subprocess.run(["gdb","--nx","--batch","-x",str(script)],stdout=subprocess.PIPE,stderr=subprocess.STDOUT,timeout=45)
                Path(f"build/gdb-{mode}.log").write_bytes(result.stdout)
                if result.returncode or b"GDB ABI DATA BSS OK" not in result.stdout: raise RuntimeError(f"GDB falhou: build/gdb-{mode}.log")
                deadline=time.monotonic()+30
                while b"Terminal pronto. Digite help." not in log.read_bytes():
                    if process.poll() is not None or time.monotonic()>deadline: raise RuntimeError("Boot apos detach falhou")
                    time.sleep(0.05)
                if b"KERNEL PANIC" in log.read_bytes(): raise RuntimeError("Panic apos GDB")
                print(f"OK: GDB/{mode}, breakpoint real em kmain, RSP%16=8, DATA/BSS e boot apos detach.",flush=True)
            finally:
                if process.poll() is None: process.kill()
                process.wait()
