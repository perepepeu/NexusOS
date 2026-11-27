"""Confirma a execucao do kernel e o handshake Limine em BIOS e UEFI."""
from pathlib import Path
import re
import subprocess

Path("build").mkdir(exist_ok=True)
firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
if not firmware.is_file():
    raise SystemExit("OVMF ausente: instale o pacote ovmf no Ubuntu.")

for mode in ("bios", "uefi"):
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
        "-m", "256M", "-cdrom", "build/nexusos.iso",
        "-boot", "d", "-display", "none", "-serial", "stdio",
        "-monitor", "none", "-no-reboot", "-net", "none",
    ]
    if mode == "uefi":
        command += ["-drive", f"if=pflash,format=raw,readonly=on,file={firmware}"]
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        try:
            output, _ = process.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            process.kill()
            output, _ = process.communicate()
    log = Path(f"build/boot-{mode}.log")
    log.write_bytes(output)
    # OVMF intercala sequencias de posicionamento entre os caracteres.
    text = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", output)
    if (b"NexusOS: kernel inicial pronto. CPU em espera." not in text
            or b"Bootloader: Limine" not in text or b"Protocolo Limine: OK" not in text
            or process.returncode != -9
            or b"PANIC" in text.upper()):
        raise SystemExit(f"FALHA: kernel em {mode}; consulte {log}")
    print(f"OK: kernel iniciado e protocolo Limine validado em {mode}; log: {log}")
