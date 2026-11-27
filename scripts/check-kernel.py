"""Verifica boot, serial e pixels do console em BIOS e UEFI."""
from pathlib import Path
import json
import re
import socket
import subprocess
import tempfile


def capture_screen(monitor_path, destination):
    with socket.socket(socket.AF_UNIX, socket.SOCK_STREAM) as connection:
        connection.settimeout(5)
        connection.connect(monitor_path)
        with connection.makefile("rwb") as stream:
            json.loads(stream.readline())  # Saudacao QMP.

            def execute(command, arguments=None):
                request = {"execute": command, "arguments": arguments or {}}
                stream.write(json.dumps(request).encode() + b"\n")
                stream.flush()
                while True:
                    line = stream.readline()
                    if not line:
                        raise RuntimeError("QEMU encerrou a conexao QMP.")
                    response = json.loads(line)
                    if "error" in response:
                        raise RuntimeError(response["error"])
                    if "return" in response:
                        return

            execute("qmp_capabilities")
            execute("screendump", {"filename": str(destination.resolve())})

    header, dimensions, maximum, pixels = destination.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if header != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise RuntimeError(f"Captura de tela invalida: {destination}")
    colors = [pixels[i:i + 3] for i in range(0, len(pixels), 3)]
    if (colors.count(bytes((16, 24, 32))) < width * height // 2
            or colors.count(bytes((220, 235, 245))) < 500):
        raise RuntimeError(f"Fundo ou texto do kernel ausente: {destination}")

Path("build").mkdir(exist_ok=True)
firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
if not firmware.is_file():
    raise SystemExit("OVMF ausente: instale o pacote ovmf no Ubuntu.")

for mode in ("bios", "uefi"):
    monitor_directory = tempfile.TemporaryDirectory(prefix="nexus-qmp-")
    monitor_path = str(Path(monitor_directory.name) / "monitor.sock")
    command = [
        "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
        "-m", "256M", "-cdrom", "build/nexusos.iso",
        "-boot", "d", "-display", "none", "-serial", "stdio",
        "-monitor", "none", "-no-reboot", "-net", "none",
        "-qmp", f"unix:{monitor_path},server=on,wait=off",
    ]
    if mode == "uefi":
        command += ["-drive", f"if=pflash,format=raw,readonly=on,file={firmware}"]
    with subprocess.Popen(command, stdout=subprocess.PIPE, stderr=subprocess.STDOUT) as process:
        try:
            output, _ = process.communicate(timeout=20)
        except subprocess.TimeoutExpired:
            try:
                capture_screen(monitor_path, Path(f"build/screen-{mode}.ppm"))
            finally:
                process.kill()
                output, _ = process.communicate()
                monitor_directory.cleanup()
    log = Path(f"build/boot-{mode}.log")
    log.write_bytes(output)
    # OVMF intercala sequencias de posicionamento entre os caracteres.
    text = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", output)
    if (b"NexusOS: kernel inicial pronto. CPU em espera." not in text
            or b"Bootloader: Limine" not in text or b"Protocolo Limine: OK" not in text
            or b"Tela: framebuffer pronto." not in text
            or process.returncode != -9
            or b"PANIC" in text.upper()):
        raise SystemExit(f"FALHA: kernel em {mode}; consulte {log}")
    print(f"OK: kernel, Limine e texto no framebuffer em {mode}; log: {log}")
