"""Confirma que o menu Limine aparece na serial em BIOS e UEFI."""
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
        "-m", "256M", "-cdrom", "build/nexusos-bootloader.iso",
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
    if (b"NexusOS - Limine" not in text or b"ENTER Boot" not in text
            or b"PANIC" in text.upper()):
        raise SystemExit(f"FALHA: menu Limine em {mode}; consulte {log}")
    print(f"OK: menu Limine carregado em {mode}; log: {log}")
