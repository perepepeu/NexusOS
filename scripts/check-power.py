"""Valida reset e power-off do guest, sem resetar/desligar pelo monitor."""
from pathlib import Path
import json
import select
import socket
import subprocess
import tempfile
import time

READY = b"Terminal pronto. Digite help."


class Monitor:
    def __init__(self, path):
        self.connection = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        self.connection.settimeout(5)
        self.connection.connect(path)
        self.stream = self.connection.makefile("rwb", buffering=0)
        self.events = []
        self.identifier = 0
        self.receive(5)
        self.execute("qmp_capabilities")

    def close(self):
        self.stream.close()
        self.connection.close()

    def receive(self, timeout):
        if not select.select([self.connection], [], [], timeout)[0]: return None
        line = self.stream.readline()
        if not line: return None
        message = json.loads(line)
        if "event" in message: self.events.append(message)
        return message

    def execute(self, command, arguments=None):
        self.identifier += 1
        identifier = self.identifier
        self.stream.write(json.dumps({"execute": command, "arguments": arguments or {},
                                      "id": identifier}).encode() + b"\n")
        deadline = time.monotonic() + 5
        while time.monotonic() < deadline:
            response = self.receive(0.1)
            if response and response.get("id") == identifier:
                if "error" in response: raise RuntimeError(response["error"])
                return response.get("return")
        raise RuntimeError(f"Timeout QMP: {command}")

    def type_command(self, value):
        for key in ["spc" if c == " " else "minus" if c == "-" else c for c in value] + ["ret"]:
            self.execute("input-send-event", {"events": [
                {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}
                for down in (True, False)]})
            time.sleep(0.03)


def wait_for(monitor, process, predicate, label, timeout=25):
    deadline = time.monotonic() + timeout
    while not predicate():
        if process.poll() is not None: raise RuntimeError(f"QEMU encerrou antes de {label}")
        if time.monotonic() > deadline: raise RuntimeError(f"Timeout: {label}")
        monitor.receive(0.05)


def check(mode, scenario, image):
    log = Path(f"build/power-{scenario}-{mode}.log")
    with tempfile.TemporaryDirectory(prefix="nexus-power-") as directory:
        path = str(Path(directory) / "qmp.sock")
        command = ["qemu-system-x86_64", "-machine", "q35", "-accel", "tcg", "-m", "256M",
                   "-cdrom", str(image), "-boot", "d", "-display", "none", "-serial", "stdio",
                   "-monitor", "none", "-net", "none", "-qmp", f"unix:{path},server=on,wait=off"]
        if mode == "uefi":
            command += ["-drive", "if=pflash,format=raw,readonly=on,file=/usr/share/OVMF/OVMF_CODE_4M.fd"]
        with log.open("wb") as output, subprocess.Popen(command, stdout=output, stderr=subprocess.STDOUT) as process:
            monitor = None
            try:
                deadline = time.monotonic() + 10
                while not Path(path).exists():
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError(f"QMP indisponivel; consulte {log}")
                    time.sleep(0.05)
                monitor = Monitor(path)
                wait_for(monitor, process, lambda: READY in log.read_bytes(), "terminal")
                monitor.events.clear()
                action = "shutdown" if scenario in ("shutdown", "unsupported") else "reboot"
                monitor.type_command(action + " extra")
                wait_for(monitor, process, lambda: f"Uso: {action} (sem argumentos).".encode() in log.read_bytes(),
                         "rejeicao de argumentos")
                if any(e["event"] in ("RESET", "SHUTDOWN") for e in monitor.events):
                    raise RuntimeError("Comando invalido alterou energia")
                monitor.type_command(action)
                if scenario == "shutdown":
                    # O evento e a saida limpa devem vir do guest; o finally so limpa em caso de falha.
                    process.wait(timeout=10)
                    while monitor.receive(0.05): pass
                    if process.returncode != 0 or not any(e["event"] == "SHUTDOWN" and
                            e.get("data", {}).get("guest") for e in monitor.events):
                        raise RuntimeError(f"Desligamento real nao confirmado; consulte {log}")
                    if b"Desligamento solicitado: ACPI S5." not in log.read_bytes():
                        raise RuntimeError("Backend S5 nao foi usado")
                elif scenario == "unsupported":
                    wait_for(monitor, process, lambda: b"Nao foi possivel desligar; terminal continua ativo." in log.read_bytes(),
                             "erro de ACPI ausente")
                    monitor.type_command("echo unsupported-ok")
                    wait_for(monitor, process, lambda: b"\r\nunsupported-ok\r\n" in log.read_bytes(), "terminal apos falha")
                    if any(e["event"] in ("RESET", "SHUTDOWN") for e in monitor.events):
                        raise RuntimeError("ACPI ausente causou evento inesperado")
                else:
                    wait_for(monitor, process, lambda: log.read_bytes().count(READY) == 2, "segundo boot")
                    if not any(e["event"] == "RESET" and e.get("data", {}).get("guest") for e in monitor.events):
                        raise RuntimeError(f"Reset do guest ausente; consulte {log}")
                    expected = b"Reset alternativo via controlador PS/2." if scenario == "fallback" else b"Reset ACPI via I/O."
                    if expected not in log.read_bytes(): raise RuntimeError("Backend de reset incorreto")
                    monitor.type_command("echo reboot-ok")
                    wait_for(monitor, process, lambda: b"\r\nreboot-ok\r\n" in log.read_bytes(), "terminal apos reboot")
                    if not monitor.execute("query-status")["running"]: raise RuntimeError("Guest nao esta executando")
                Path(f"build/power-{scenario}-{mode}-events.json").write_text(json.dumps(monitor.events, indent=2))
                if b"KERNEL PANIC" in log.read_bytes(): raise RuntimeError(f"Panic inesperado; consulte {log}")
                print(f"OK: {scenario}/{mode}; eventos e logs em build/power-{scenario}-{mode}*", flush=True)
            finally:
                if monitor: monitor.close()
                if process.poll() is None: process.kill()
                process.wait()


def main():
    config = Path("build/limine-power-fallback.conf")
    config.write_text(Path("boot/limine.conf").read_text() + "\n    cmdline: selftest=power-fallback\n")
    fallback = Path("build/nexusos-power-fallback.iso")
    result = subprocess.run(["bash", "scripts/build-iso.sh", str(config), str(fallback)],
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    if result.returncode: raise RuntimeError(result.stdout.decode(errors="replace"))
    for scenario in ("shutdown", "reboot", "fallback", "unsupported"):
        for mode in ("bios", "uefi"):
            check(mode, scenario, fallback if scenario in ("fallback", "unsupported") else Path("build/nexusos.iso"))


if __name__ == "__main__": main()
