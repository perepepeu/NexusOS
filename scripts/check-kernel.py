"""Verifica boot, serial e pixels do console em BIOS e UEFI."""
from pathlib import Path
import argparse
import json
import re
import socket
import subprocess
import tempfile
import time


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

def boot(mode, scenario, image, firmware):
    suffix = mode if scenario == "normal" else f"{scenario}-{mode}"
    log = Path(f"build/boot-{suffix}.log")
    fatal = scenario in ("panic", "assert")
    marker = (b"Sistema interrompido. Reinicie a maquina." if fatal else
              b"NexusOS: kernel inicial pronto. CPU em espera.")
    with tempfile.TemporaryDirectory(prefix="nexus-qmp-") as directory:
        monitor_path = str(Path(directory) / "monitor.sock")
        command = [
            "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
            "-m", "256M", "-cdrom", str(image), "-boot", "d", "-display", "none",
            "-serial", "stdio", "-monitor", "none", "-no-reboot", "-net", "none",
            "-qmp", f"unix:{monitor_path},server=on,wait=off",
        ]
        if mode == "uefi":
            command += ["-drive", f"if=pflash,format=raw,readonly=on,file={firmware}"]
        with log.open("wb") as output, subprocess.Popen(
                command, stdout=output, stderr=subprocess.STDOUT) as process:
            try:
                deadline = time.monotonic() + 20
                while marker not in log.read_bytes():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"Kernel nao concluiu {scenario}/{mode}; consulte {log}")
                    time.sleep(0.1)
                # O guest deve permanecer ativo, inclusive apos um panic.
                try:
                    process.wait(timeout=0.3)
                except subprocess.TimeoutExpired:
                    pass
                else:
                    raise RuntimeError(f"VM encerrou inesperadamente; consulte {log}")
                capture_screen(monitor_path, Path(f"build/screen-{suffix}.ppm"))
            finally:
                if process.poll() is None: process.kill()
                process.wait()

    text = re.sub(rb"\x1b\[[0-?]*[ -/]*[@-~]", b"", log.read_bytes())
    required = [b"Bootloader: Limine", b"Protocolo Limine: OK", b"Tela: framebuffer pronto."]
    records = re.findall(rb"\[(\d+)\] \[(?:DEBUG|INFO|WARN|ERROR|FATAL)\]", text)
    if list(map(int, records)) != list(range(1, len(records) + 1)):
        raise RuntimeError(f"Sequencia de logs incorreta; consulte {log}")
    if fatal:
        code = 3 if scenario == "panic" else 4
        required += [f"[FATAL] [panic] KERNEL PANIC codigo={code}:".encode(), marker]
        if (b"kernel inicial pronto" in text or
                not re.search(rb"Origem: kernel/main.c:\d+", text)):
            raise RuntimeError(f"Panic sem origem ou boot continuou; consulte {log}")
        if scenario == "assert": required += [b"Assert falhou: false"]
    else:
        required += [marker]
        if b"PANIC" in text.upper() or b"[FATAL]" in text:
            raise RuntimeError(f"Falha fatal inesperada; consulte {log}")
    if scenario == "logs":
        required += [b"[DEBUG] [test] DEBUG habilitado",
                     b"Formato: ok 42 2a 0x1234 % (null)",
                     b"[WARN] [test] Aviso simulado",
                     b"[ERROR] [test] Erro recuperavel simulado"]
        if b"DEBUG oculto" in text:
            raise RuntimeError(f"Filtro de log falhou; consulte {log}")
    if any(message not in text for message in required):
        raise RuntimeError(f"Diagnostico incompleto; consulte {log}")
    print(f"OK: {scenario}/{mode}, serial e framebuffer; log: {log}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--errors", action="store_true", help="Testa logs, panic e assert.")
    args = parser.parse_args()
    Path("build").mkdir(exist_ok=True)
    firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    if not firmware.is_file(): raise SystemExit("OVMF ausente: instale o pacote ovmf.")
    scenarios = ("logs", "panic", "assert") if args.errors else ("normal",)
    for scenario in scenarios:
        image = Path("build/nexusos.iso")
        if scenario != "normal":
            config = Path(f"build/limine-{scenario}.conf")
            config.write_text(Path("boot/limine.conf").read_text() +
                              f"\n    cmdline: selftest={scenario}\n")
            image = Path(f"build/nexusos-{scenario}.iso")
            result = subprocess.run(["bash", "scripts/build-iso.sh", str(config), str(image)],
                                    stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
            if result.returncode:
                raise RuntimeError(result.stdout.decode(errors="replace"))
        for mode in ("bios", "uefi"):
            boot(mode, scenario, image, firmware)


if __name__ == "__main__":
    main()
