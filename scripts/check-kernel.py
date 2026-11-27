"""Verifica boot, serial e pixels do console em BIOS e UEFI."""
from pathlib import Path
import argparse
import json
import re
import socket
import subprocess
import tempfile
import time


def qmp_execute(monitor_path, command, arguments=None):
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
            execute(command, arguments)


def capture_screen(monitor_path, destination):
    qmp_execute(monitor_path, "screendump", {"filename": str(destination.resolve())})

    header, dimensions, maximum, pixels = destination.read_bytes().split(b"\n", 3)
    width, height = map(int, dimensions.split())
    if header != b"P6" or maximum != b"255" or len(pixels) != width * height * 3:
        raise RuntimeError(f"Captura de tela invalida: {destination}")
    colors = [pixels[i:i + 3] for i in range(0, len(pixels), 3)]
    if (colors.count(bytes((16, 24, 32))) < width * height // 2
            or colors.count(bytes((220, 235, 245))) < 500):
        raise RuntimeError(f"Fundo ou texto do kernel ausente: {destination}")


def type_keyboard_test(monitor_path):
    def events(items):
        qmp_execute(monitor_path, "input-send-event", {"events": [
            {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}
            for key, down in items]})
        time.sleep(0.03)

    def chord(*keys):
        events([(key, True) for key in keys] + [(key, False) for key in reversed(keys)])

    chord("a")
    chord("shift", "b")
    chord("caps_lock")
    chord("c")
    chord("shift", "d")
    chord("caps_lock")
    chord("1")
    chord("shift", "1")
    chord("backspace")
    chord("minus")
    chord("ret")
    chord("ctrl", "a")
    chord("alt", "b")
    chord("ctrl_r", "a")
    chord("alt_r", "b")
    chord("up")
    events([("shift", True), ("shift_r", True), ("shift", False)])
    chord("a")
    events([("shift_r", False)])
    chord("ret")
    chord("esc")

def boot(mode, scenario, image, firmware):
    suffix = mode if scenario == "normal" else f"{scenario}-{mode}"
    log = Path(f"build/boot-{suffix}.log")
    exceptions = {"divide": (0, "Divide error", "0x0"),
                  "opcode": (6, "Invalid opcode", "0x0"),
                  "pagefault": (14, "Page fault", "0x0"),
                  "protection": (13, "General protection", "0xfff8"),
                  "doublefault": (8, "Double fault", "0x0")}
    fatal = scenario in ("panic", "assert") or scenario in exceptions
    marker = (b"Sistema interrompido. Reinicie a maquina." if fatal else
              b"code=1 down=0 mods=0 char=0" if scenario == "keyboard" else
              b"[keyboard] Entrada: A" if scenario == "keyboard-echo" else
              b"Teclado pronto. Digite e pressione Enter.")
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
                injected = False
                while marker not in log.read_bytes():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"Kernel nao concluiu {scenario}/{mode}; consulte {log}")
                    if (scenario in ("keyboard", "keyboard-echo") and not injected and
                            b"Teclado pronto. Digite e pressione Enter." in log.read_bytes()):
                        type_keyboard_test(monitor_path)
                        injected = True
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
    required = [b"Bootloader: Limine", b"Protocolo Limine: OK", b"Tela: framebuffer pronto.",
                b"GDT e TSS carregadas.", b"IDT carregada: 256 vetores.",
                b"Interrupcoes habilitadas.", b"Timer IRQ0 confirmado:"]
    records = re.findall(rb"\[(\d+)\] \[(?:DEBUG|INFO|WARN|ERROR|FATAL)\]", text)
    if list(map(int, records)) != list(range(1, len(records) + 1)):
        raise RuntimeError(f"Sequencia de logs incorreta; consulte {log}")
    if fatal:
        code = 5 if scenario in exceptions else 3 if scenario == "panic" else 4
        required += [f"[FATAL] [panic] KERNEL PANIC codigo={code}:".encode(), marker]
        if (b"kernel inicial pronto" in text or
                (scenario not in exceptions and not re.search(rb"Origem: kernel/main.c:\d+", text))):
            raise RuntimeError(f"Panic sem origem ou boot continuou; consulte {log}")
        if scenario == "assert": required += [b"Assert falhou: false"]
        if scenario in exceptions:
            vector, name, error = exceptions[scenario]
            required += [name.encode(), f"Vetor={vector} Erro={error}".encode()]
            if not re.search(rb"RIP=0xffffffff[0-9a-f]+ RSP=0xffffffff[0-9a-f]+", text):
                raise RuntimeError(f"Contexto da CPU ausente; consulte {log}")
            if scenario == "pagefault": required += [b"CR2=0x400000000000"]
            if scenario == "doublefault": required += [b"IST double fault ativa."]
    else:
        required += [marker, b"PS/2 pronto: IRQ1, scancodes set 1, layout US.",
                     b"NexusOS: kernel inicial pronto. CPU em espera."]
        if b"PANIC" in text.upper() or b"[FATAL]" in text:
            raise RuntimeError(f"Falha fatal inesperada; consulte {log}")
    if scenario == "logs":
        required += [b"[DEBUG] [test] DEBUG habilitado",
                     b"Formato: ok 42 2a 0x1234 % (null)",
                     b"[WARN] [test] Aviso simulado",
                     b"[ERROR] [test] Erro recuperavel simulado"]
        if b"DEBUG oculto" in text:
            raise RuntimeError(f"Filtro de log falhou; consulte {log}")
    if scenario == "interrupts":
        required += [b"Breakpoint INT3 tratado; retornando por IRETQ.",
                     b"GDT IDT INT3 registros e PIT: OK."]
    if scenario in ("keyboard", "keyboard-echo"):
        required += [b"[keyboard] Entrada: aBCd1-", b"[keyboard] Entrada: A"]
        if b"Descartes acumulados" in text:
            raise RuntimeError(f"Teclas descartadas durante o teste; consulte {log}")
    if scenario == "keyboard":
        required += [b"code=1e down=1 mods=0 char=97", b"code=1e down=0 mods=0 char=0",
                     b"code=30 down=1 mods=1 char=66", b"code=2e down=1 mods=8 char=67",
                     b"code=20 down=1 mods=9 char=100", b"code=2 down=1 mods=1 char=33",
                     b"code=e down=1 mods=0 char=8", b"code=1e down=1 mods=2 char=97",
                     b"code=30 down=1 mods=4 char=98", b"code=11d down=0 mods=0 char=0",
                     b"code=138 down=0 mods=0 char=0", b"code=148 down=1 mods=0 char=0",
                     b"code=148 down=0 mods=0 char=0", b"code=1e down=1 mods=1 char=65"]
    if any(message not in text for message in required):
        raise RuntimeError(f"Diagnostico incompleto; consulte {log}")
    print(f"OK: {scenario}/{mode}, serial e framebuffer; log: {log}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--errors", action="store_true", help="Testa logs, panic e assert.")
    group.add_argument("--interrupts", action="store_true", help="Testa tabelas, IRQs e excecoes.")
    group.add_argument("--keyboard", action="store_true", help="Injeta teclas PS/2 pelo QMP.")
    args = parser.parse_args()
    Path("build").mkdir(exist_ok=True)
    firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    if not firmware.is_file(): raise SystemExit("OVMF ausente: instale o pacote ovmf.")
    scenarios = (("keyboard", "keyboard-echo") if args.keyboard else ("logs", "panic", "assert") if args.errors else
                 ("interrupts", "divide", "opcode", "pagefault", "protection", "doublefault")
                 if args.interrupts else ("normal",))
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
