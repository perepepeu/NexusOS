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


def type_terminal_test(monitor_path, limits=False, memory=False, pmm=False, vmm=False, heap=False, memtest=False, processes=False, process_log=None, scheduler=False, user=False, syscalls=False, interactive=False, elf=False, diagnostics=False):
    def chord(*keys):
        items = [(key, True) for key in keys] + [(key, False) for key in reversed(keys)]
        qmp_execute(monitor_path, "input-send-event", {"events": [
            {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}
            for key, down in items]})
        time.sleep(0.03)

    def text(value):
        mapping = {" ": "spc", "-": "minus", ".": "dot"}
        for character in value:
            if character.isupper(): chord("shift", character.lower())
            else: chord(mapping.get(character, character))

    def command(value):
        text(value); chord("ret")

    if diagnostics:
        def wait_log(wanted, timeout=10):
            deadline = time.monotonic() + timeout
            while wanted not in process_log.read_bytes():
                if time.monotonic() >= deadline: raise RuntimeError(f"Diagnostico ausente: {wanted!r}")
                time.sleep(0.03)
        command("programs"); command("run busy"); command("run busy"); command("ps"); command("top")
        wait_log(b"Top: amostra concluida.")
        created = re.findall(rb"Processo ELF criado: (\d+) \(busy\)\r?\n", process_log.read_bytes())
        if len(created) != 2 or created[0] == created[1]: raise RuntimeError("PIDs dos workers CPU invalidos")
        for pid in created:
            command("kill " + pid.decode()); wait_log(b"Processo encerrado: " + pid + b"\r\n")
            command("wait " + pid.decode()); wait_log(b"Processo coletado: " + pid + b" status=-9")
        command("kill 0"); command("kill 1"); command("kill 99999"); command("kill 18446744073709551616")
        command("run fault")
        deadline = time.monotonic() + 8
        while True:
            ids = re.findall(rb"Processo ELF criado: (\d+) \(fault\)\r?\n", process_log.read_bytes())
            if ids: break
            if time.monotonic() >= deadline: raise RuntimeError("PID completo de fault ausente")
            time.sleep(0.03)
        command("wait " + ids[-1].decode()); wait_log(b"Processo coletado: " + ids[-1] + b" status=-270")
        command("elfdemo"); wait_log(b"ELF demo: alpha=0 beta=0")
        command("top extra"); command("ps"); command("pmm"); command("vmm"); command("heap")
        command("echo diagnostics-ok")
        return
    if elf:
        command("programs"); command("elfdemo")
        deadline = time.monotonic() + 10
        while b"ELF demo: alpha=0 beta=0" not in process_log.read_bytes():
            if time.monotonic() >= deadline: raise RuntimeError("elfdemo nao concluiu os dois programas")
            time.sleep(0.03)
        command("run alpha"); command("run beta")
        deadline = time.monotonic() + 8
        while True:
            created = re.findall(rb"Processo ELF criado: (\d+) \((alpha|beta)\)\r?\n", process_log.read_bytes())
            if len(created) == 2: break
            if time.monotonic() >= deadline: raise RuntimeError("run nao retornou os PIDs completos")
            time.sleep(0.03)
        if created[0][0] == created[1][0]: raise RuntimeError("run reutilizou PID vivo")
        for pid, _ in created:
            command("wait " + pid.decode())
            deadline = time.monotonic() + 8
            wanted = b"Processo coletado: " + pid + b" status=0"
            while wanted not in process_log.read_bytes():
                if time.monotonic() >= deadline: raise RuntimeError("wait do ELF nao retornou status=0")
                time.sleep(0.03)
        command("run"); command("run inexistente"); command("run alpha beta")
        command("ps"); command("pmm"); command("vmm"); command("heap"); command("echo elf-ok")
        return
    if syscalls:
        if interactive:
            command("sysdemo")
            deadline = time.monotonic() + 5
            while b"Syscalls Ring 3: digite uma linha" not in process_log.read_bytes():
                if time.monotonic() >= deadline: raise RuntimeError("sysdemo nao iniciou")
                time.sleep(0.03)
            command("ola")
            deadline = time.monotonic() + 5
            while b"Sysdemo concluido: status=0" not in process_log.read_bytes():
                if time.monotonic() >= deadline: raise RuntimeError("sysdemo nao concluiu")
                time.sleep(0.03)
        command("pmm"); command("vmm"); command("heap"); command("echo syscalls-ok")
        return
    if user:
        command("spawnuser")
        deadline = time.monotonic() + 3
        while time.monotonic() < deadline:
            # A serial pode estar no meio de um PID (ex.: "2" antes de "29").
            # Esperar o sufixo impede enviar wait para um numero incompleto.
            ids = re.findall(rb"Processo Ring3 criado: (\d+) \(saida esperada: 42\)", process_log.read_bytes())
            if ids: break
            time.sleep(0.03)
        else: raise RuntimeError("spawnuser nao retornou PID")
        command("wait " + ids[-1].decode()); time.sleep(0.5)
        command("ps"); command("sched"); command("pmm"); command("vmm"); command("heap")
        command("echo user-ok")
        return
    if scheduler:
        command("sched")
        command("ps")
        command("pmm")
        command("vmm")
        command("heap")
        command("echo scheduler-ok")
        return
    if processes:
        def created(number):
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                ids = re.findall(rb"Processo criado: (\d+)", process_log.read_bytes())
                if len(ids) >= number: return ids[number - 1].decode()
                time.sleep(0.03)
            raise RuntimeError("spawn nao retornou PID")
        command("ps")
        command("threads")
        command("spawn extra")
        command("spawn")
        command("wait " + created(1))
        time.sleep(5.3)  # Aguarda o filho sem inundar a fila PS/2 enquanto o terminal bloqueia.
        command("spawn")
        pid = created(2)
        command("kill " + pid)
        command("wait " + pid)
        command("wait " + pid)
        command("kill 1")
        command("kill 99999")
        command("wait 1")
        command("kill 18446744073709551616")
        command("ps")
        command("pmm")
        command("vmm")
        command("heap")
        command("echo processes-ok")
        return
    if memtest:
        command("meminfo")
        command("memtest extra")
        command("memtest")
        command("memtest")
        command("pmm")
        command("vmm")
        command("heap")
        command("echo memtest-ok")
        return
    if heap:
        command("heap")
        command("vmm")
        command("pmm")
        command("echo heap-ok")
        return
    if vmm:
        command("vmm")
        command("pmm")
        command("echo vmm-ok")
        return
    if pmm:
        command("pmm")
        command("memory")
        command("echo pmm-ok")
        return
    if memory:
        command("memory")
        command("memmap")
        command("sysinfo")
        command("echo memory-ok")
        return
    if limits:
        chord("ret")
        command("  echo   espacos ")
        command("echo " + "x" * 122 + "zzzz")
        command("echo limits-ok")
        return
    command("HELP")
    command("version")
    command("uptime")
    command("sysinfo")
    command("echo ola nexus")
    text("echo errado"); chord("ctrl", "u"); command("echo limpo")
    text("echo ac"); chord("left"); text("b"); chord("end"); chord("ret")
    text("echo axbc"); chord("home")
    for _ in range(6): chord("right")
    chord("delete"); chord("end"); chord("ret")
    text("echo abxc"); chord("left"); chord("backspace"); chord("end"); chord("ret")
    text("echo draft"); chord("up"); chord("down"); chord("ret")
    text("echo cancelado"); chord("ctrl", "c")
    command("naoexiste")
    command("help extra")
    command("history")
    command("clear")
    command("version")
    text("echo mantido"); chord("ctrl", "l"); chord("ret")
    command("echo terminal-ok")

def boot(mode, scenario, image, firmware, ram_mib=256):
    suffix = mode if scenario == "normal" else f"{scenario}-{mode}"
    if scenario in ("memory", "pmm", "vmm", "heap", "memtest", "processes", "scheduler", "user", "user-faults", "syscalls", "sysread", "elf", "diagnostics"): suffix = f"{scenario}-{ram_mib}-{mode}"
    log = Path(f"build/boot-{suffix}.log")
    exceptions = {"divide": (0, "Divide error", "0x0"),
                  "opcode": (6, "Invalid opcode", "0x0"),
                  "pagefault": (14, "Page fault", "0x0"),
                  "protection": (13, "General protection", "0xfff8"),
                  "doublefault": (8, "Double fault", "0x0"),
                  "vmm-unmapped": (14, "Page fault", "0x0"),
                  "vmm-readonly": (14, "Page fault", "0x3"),
                  "vmm-nx": (14, "Page fault", "0x11"),
                  "access-null": (14, "Page fault", "0x0"),
                  "access-guard-low": (14, "Page fault", "0x2"),
                  "access-guard-high": (14, "Page fault", "0x2"),
                  "access-unmapped": (14, "Page fault", "0x0"),
                  "access-nx": (14, "Page fault", "0x11"),
                  "process-guard": (14, "Page fault", "0x2")}
    heap_errors = {"access-overflow", "access-underflow", "access-freed", "access-doublefree", "access-badfree"}
    fatal = scenario in ("panic", "assert") or scenario in exceptions or scenario in heap_errors
    marker = (b"Sistema interrompido. Reinicie a maquina." if fatal else
              b"code=1 down=0 mods=0 char=0" if scenario == "keyboard" else
              b"[keyboard] Entrada: A" if scenario == "keyboard-echo" else
              b"\r\nterminal-ok\r\n" if scenario == "terminal" else
              b"\r\nlimits-ok\r\n" if scenario == "terminal-limits" else
              b"\r\nmemory-ok\r\n" if scenario == "memory" else
              b"\r\npmm-ok\r\n" if scenario == "pmm" else
              b"\r\nvmm-ok\r\n" if scenario == "vmm" else
              b"\r\nheap-ok\r\n" if scenario == "heap" else
              b"\r\nmemtest-ok\r\n" if scenario == "memtest" else
              b"\r\nprocesses-ok\r\n" if scenario == "processes" else
              b"\r\nscheduler-ok\r\n" if scenario == "scheduler" else
              b"\r\nuser-ok\r\n" if scenario in ("user", "user-faults") else
              b"\r\nsyscalls-ok\r\n" if scenario in ("syscalls", "sysread") else
              b"\r\nelf-ok\r\n" if scenario == "elf" else
              b"\r\ndiagnostics-ok\r\n" if scenario == "diagnostics" else
              b"Terminal pronto. Digite help.")
    with tempfile.TemporaryDirectory(prefix="nexus-qmp-") as directory:
        monitor_path = str(Path(directory) / "monitor.sock")
        command = [
            "qemu-system-x86_64", "-machine", "q35", "-accel", "tcg",
            "-m", f"{ram_mib}M", "-cdrom", str(image), "-boot", "d", "-display", "none",
            "-serial", "stdio", "-monitor", "none", "-no-reboot", "-net", "none",
            "-qmp", f"unix:{monitor_path},server=on,wait=off",
        ]
        if mode == "uefi":
            command += ["-drive", f"if=pflash,format=raw,readonly=on,file={firmware}"]
        with log.open("wb") as output, subprocess.Popen(
                command, stdout=output, stderr=subprocess.STDOUT) as process:
            try:
                deadline = time.monotonic() + (100 if scenario == "diagnostics" else 60 if scenario in ("user-faults", "syscalls", "sysread", "elf") else 50 if scenario == "processes" else 40 if scenario in ("pmm", "scheduler", "user") else 20)
                injected = False
                read_phase = 0
                while marker not in log.read_bytes():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"Kernel nao concluiu {scenario}/{mode}; consulte {log}")
                    if scenario == "sysread" and read_phase < 2:
                        wanted = f"Sys_read bloqueado: fase={read_phase + 1};".encode()
                        if wanted in log.read_bytes():
                            keys = ("a", "b", "c", "ret") if read_phase == 0 else ("z",)
                            qmp_execute(monitor_path, "input-send-event", {"events": [
                                {"type": "key", "data": {"down": down, "key": {"type": "qcode", "data": key}}}
                                for key in keys for down in (True, False)]})
                            read_phase += 1
                    if (scenario in ("keyboard", "keyboard-echo") and not injected and
                            b"Teclado pronto. Digite e pressione Enter." in log.read_bytes()):
                        type_keyboard_test(monitor_path)
                        injected = True
                    if (scenario in ("terminal", "terminal-limits") and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, scenario == "terminal-limits")
                        injected = True
                    if (scenario == "memory" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, memory=True)
                        injected = True
                    if (scenario == "pmm" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, pmm=True)
                        injected = True
                    if (scenario == "vmm" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, vmm=True)
                        injected = True
                    if (scenario == "heap" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, heap=True)
                        injected = True
                    if (scenario == "memtest" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, memtest=True)
                        injected = True
                    if (scenario == "processes" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, processes=True, process_log=log)
                        injected = True
                    if (scenario == "scheduler" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, scheduler=True)
                        injected = True
                    if (scenario in ("user", "user-faults") and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, user=True, process_log=log)
                        injected = True
                    if (scenario in ("syscalls", "sysread") and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, syscalls=True, interactive=scenario == "sysread", process_log=log)
                        injected = True
                    if (scenario == "elf" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, elf=True, process_log=log)
                        injected = True
                    if (scenario == "diagnostics" and not injected and
                            b"Terminal pronto. Digite help." in log.read_bytes()):
                        type_terminal_test(monitor_path, diagnostics=True, process_log=log)
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
    required += [b"[INFO] [memory] Mapa fisico:"]
    required += [b"[INFO] [pmm] Alocador pronto:"]
    required += [b"VMM pronto: CR3="]
    baseline = re.search(rb"\[process\] Base: alocadas=(\d+) mapeadas=(\d+) tabelas=(\d+)", text)
    allocated, mapped_pages, tables = map(int, baseline.groups()) if baseline else (1, 0, 1)
    memory_baseline = [f"Paginas alocadas: {allocated}".encode(), f"Paginas mapeadas: {mapped_pages}".encode(),
                       f"Tabelas proprias: {tables} paginas".encode()]
    if not fatal and not baseline: raise RuntimeError(f"Baseline do gerenciador ausente; consulte {log}")
    records = re.findall(rb"\[(\d+)\] \[(?:DEBUG|INFO|WARN|ERROR|FATAL)\]", text)
    if list(map(int, records)) != list(range(1, len(records) + 1)):
        raise RuntimeError(f"Sequencia de logs incorreta; consulte {log}")
    if fatal:
        code = 5 if scenario in exceptions else 10 if scenario in heap_errors else 3 if scenario == "panic" else 4
        required += [f"[FATAL] [panic] KERNEL PANIC codigo={code}:".encode(), marker]
        if (b"kernel inicial pronto" in text or
                (scenario not in exceptions and not re.search(
                    rb"Origem: kernel/heap.c:\d+" if scenario in heap_errors else rb"Origem: kernel/main.c:\d+", text))):
            raise RuntimeError(f"Panic sem origem ou boot continuou; consulte {log}")
        if scenario == "assert": required += [b"Assert falhou: false"]
        if scenario in exceptions:
            vector, name, error = exceptions[scenario]
            required += [name.encode(), f"Vetor={vector} Erro={error}".encode()]
            if not re.search(rb"RIP=0xffff[0-9a-f]+ RSP=0xffff[0-9a-f]+", text):
                raise RuntimeError(f"Contexto da CPU ausente; consulte {log}")
            if scenario == "pagefault": required += [b"CR2=0x400000000000"]
            if scenario == "doublefault": required += [b"IST double fault ativa."]
            if scenario.startswith("vmm-"):
                window = re.search(rb"janela=0x([0-9a-f]+)", text)
                if not window: raise RuntimeError(f"Janela virtual ausente; consulte {log}")
                required += [f"CR2=0x{int(window[1], 16) + 0x5000:x}".encode(),
                             b"VMM: CR3, aliases, isolamento, permissoes e limpeza OK."]
            if vector == 14:
                error_bits = int(error, 16)
                access = "execucao" if error_bits & 16 else "escrita" if error_bits & 2 else "leitura"
                cause = "protecao violada" if error_bits & 1 else "pagina ausente"
                required += [f"Acesso={access} Modo=supervisor Causa={cause}".encode(), b"RSVD=0 PK=0 SS=0 SGX=0"]
            if scenario.startswith("access-"):
                target = re.search(rb"Acesso invalido em (0x[0-9a-f]+)", text)
                if not target: raise RuntimeError(f"Endereco de teste ausente; consulte {log}")
                required += [b"CR2=" + target[1]]
                region = ("pagina nula" if scenario == "access-null" else
                          "pagina livre do heap" if scenario == "access-unmapped" else
                          "pagina de dados do heap" if scenario == "access-nx" else "pagina de guarda do heap")
                required += [f"Regiao: {region}".encode()]
            if scenario == "process-guard":
                target = re.search(rb"Pilha de processo: guarda em (0x[0-9a-f]+) PID=2", text)
                if not target: raise RuntimeError(f"Guarda de pilha ausente; consulte {log}")
                required += [b"CR2=" + target[1], b"[FATAL] [cpu] PID=2"]
        if scenario in heap_errors:
            reason = ("escrita em bloco livre (use-after-free)" if scenario == "access-freed" else
                      "ponteiro invalido ou double free" if scenario in ("access-doublefree", "access-badfree") else
                      "limites ou metadados do heap corrompidos")
            required += [reason.encode(), b"[FATAL] [memory] Endereco detectado=0x"]
    else:
        required += [marker, b"PS/2 pronto: IRQ1, scancodes set 1, layout US.",
                     b"NexusOS: kernel inicial pronto. CPU em espera."]
        if b"PANIC" in text.upper() or b"[FATAL]" in text:
            raise RuntimeError(f"Falha fatal inesperada; consulte {log}")
    if scenario == "logs":
        required += [b"[DEBUG] [test] DEBUG habilitado",
                     b"Formato: ok 42 2a 0x1234 % (null)",
                     b"Inteiro 64 bits: 4294967303",
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
    if scenario == "terminal":
        required += [b"Comandos: help clear echo version uptime sysinfo history",
                     f"NexusOS {Path('VERSION').read_text().strip()} (x86_64)".encode(),
                     b"Arquitetura: x86_64", b"Timer: PIT 100 Hz",
                     b"\r\nola nexus\r\n", b"\r\nlimpo\r\n", b"\r\ndraft\r\n",
                     b"Comando desconhecido: naoexiste.", b"Uso: help (sem argumentos).",
                     b"\r\nmantido\r\n", b"^C"]
        if (text.count(b"\r\nabc\r\n") != 3 or b"\r\ncancelado\r\n" in text
                or b"\r\nerrado\r\n" in text or not re.search(rb"Uptime: \d+\.\d{2}s", text)
                or len(re.findall(rb"(?:^|\n)[1-8] ", text)) != 8):
            raise RuntimeError(f"Edicao, cancelamento, historico ou uptime incorretos; consulte {log}")
    if scenario == "terminal-limits":
        required += [b"Limite de 127 caracteres por linha.", b"\r\n" + b"x" * 122 + b"\r\n",
                     b"\r\nespacos \r\n"]
        if b"\r\n" + b"x" * 122 + b"z" in text:
            raise RuntimeError(f"Limite de entrada ultrapassado; consulte {log}")
    if scenario == "memory":
        rows = re.findall(rb"Regiao (\d+): base=0x([0-9a-f]+) tamanho=(\d+) tipo=(\d+) ([A-Z_]+)", text)
        if not rows or [int(row[0]) for row in rows] != list(range(len(rows))):
            raise RuntimeError(f"Mapa de memoria incompleto; consulte {log}")
        usable = [(int(base, 16), int(size)) for _, base, size, kind, _ in rows if int(kind) == 0]
        expected_bytes = sum(size for _, size in usable)
        expected_pages = sum(max(0, (base + size) // 4096 - max(1, (base + 4095) // 4096))
                             for base, size in usable)
        summary = re.search(rb"Utilizavel no boot: (\d+) bytes", text)
        pages = re.search(rb"Paginas utilizaveis de 4 KiB: (\d+)", text)
        if (not summary or not pages or int(summary[1]) != expected_bytes or int(pages[1]) != expected_pages
                or not ram_mib * 1024 * 1024 // 2 < expected_bytes < ram_mib * 1024 * 1024):
            raise RuntimeError(f"Contagem de RAM incorreta para {ram_mib} MiB; consulte {log}")
        if ram_mib > 4096 and (expected_bytes <= 2**32 or max(base + size for base, size in usable) <= 2**32):
            raise RuntimeError(f"Memoria acima de 4 GiB truncada; consulte {log}")
        required += [b"Bootloader e ACPI ainda nao foram reclamados.", b"Memoria utilizavel:"]
        ram_intervals = sorted((int(base, 16), int(base, 16) + int(size))
                               for _, base, size, kind, _ in rows if int(kind) in (0, 2, 3, 4, 5, 6))
        ram_end, total_ram = 0, 0
        for start, end in ram_intervals:
            total_ram += max(0, end - max(start, ram_end))
            ram_end = max(ram_end, end)
        total = re.search(rb"RAM total identificada: (\d+) bytes", text)
        if not total or int(total[1]) != total_ram or total_ram < expected_bytes:
            raise RuntimeError(f"Total RAM diverge da uniao do mapa; consulte {log}")
    if scenario == "pmm":
        required += [b"PMM: alocacao, alinhamento, zero, liberacao e protecoes OK."]
        if ram_mib > 4096: required += [b"PMM acima de 4 GiB: escrita, leitura e liberacao OK."]
        values = {}
        for name in ("totais", "livres", "alocadas", "de metadados"):
            match = re.search(f"Paginas {name}: (\\d+)".encode(), text)
            if not match: raise RuntimeError(f"Estatisticas PMM ausentes; consulte {log}")
            values[name] = int(match[1])
        if (values["totais"] != values["livres"] + values["alocadas"] + values["de metadados"]
                or values["alocadas"] != allocated or values["de metadados"] == 0):
            raise RuntimeError(f"Contadores PMM incorretos; consulte {log}")
        bitmap = re.search(rb"Bitmap fisico: 0x([0-9a-f]+)", text)
        if not bitmap or int(bitmap[1], 16) == 0 or int(bitmap[1], 16) % 4096:
            raise RuntimeError(f"Bitmap PMM invalido; consulte {log}")
    if scenario == "vmm":
        required += [b"VMM: CR3, aliases, isolamento, permissoes e limpeza OK.",
                     b"Memoria virtual do kernel - 4 niveis"] + memory_baseline
        if ram_mib > 4096: required += [b"VMM: frame acima de 4 GiB mapeado e acessado OK."]
    if scenario == "heap":
        required += [b"Heap: alinhamento, zero, realloc, fusao e devolucao ao PMM/VMM OK.",
                     b"Integridade do heap: OK.",
                     b"Heap do kernel - alinhamento 16 bytes", b"Alocacoes ativas: 0",
                     b"Arenas ativas: 0", b"Bytes solicitados: 0 bytes", b"Bytes mapeados: 0 bytes",
                     ] + memory_baseline
    if scenario == "memtest":
        required += [b"RAM total identificada:", b"Uso: memtest (sem argumentos).", b"Integridade do heap: OK.",
                     b"Alocacoes ativas: 0", b"Arenas ativas: 0", b"Bytes mapeados: 0 bytes"] + memory_baseline
        if text.count(b"Memtest: PMM, VMM e heap OK; sem vazamentos.") != 2 or b"Memtest: FALHOU" in text:
            raise RuntimeError(f"Memtest falhou/repeticao deixou vazamentos; consulte {log}")
    if scenario == "processes":
        required += [b"Processos: contexto, round-robin, sono, wait, kill, guardas e limpeza OK.",
                     b"Threads: TIDs distintos, PID compartilhado, join e ultima thread OK.",
                     b"Recursos: heap automatico, paginas, mapas, destrutores, transferencia, kill e OOM OK.",
                     b"Idle: PID/TID 0, pilha propria, HLT, despertar e protecao OK.",
                     b"Processos preemptivos", b"PID PPID ESTADO THREADS TICKS SAIDA NOME",
                     b"Threads do kernel", b"TID PID ESTADO TICKS SAIDA NOME", b"Uso: spawn (sem argumentos).",
                     b"Filho inexistente ou ja coletado.", b"Processo inexistente, encerrado ou protegido.",
                     b"Uso: kill PID (inteiro positivo).", b"Alocacoes ativas: 0"] + memory_baseline
        idle_row = re.search(rb"(?:^|\n)0 0 READY 1 (\d+) 0 idle", text)
        if not idle_row or not int(idle_row[1]): raise RuntimeError(f"Idle nao executou; consulte {log}")
        ids = re.findall(rb"Processo criado: (\d+)", text)
        if len(ids) != 2 or ids[0] == ids[1]: raise RuntimeError("PIDs de spawn invalidos")
        required += [b"Processo coletado: " + ids[0] + b" status=0", b"Processo encerrado: " + ids[1],
                     b"Processo coletado: " + ids[1] + b" status=-9"]
    if scenario == "scheduler":
        required += [b"Preempcao: CPU sem yield, 15 GPRs, RFLAGS/DF, quantum, suspensao e regioes criticas OK.",
                     b"Escalonador: fila FIFO, concorrencia PMM/VMM/heap e ausencia de vazamentos OK.",
                     b"Concorrencia de memoria: worker=1 OK.", b"Concorrencia de memoria: worker=2 OK.",
                     b"Escalonador: Round Robin preemptivo", b"Quantum: 5 ticks (50 ms)",
                     b"Alocacoes ativas: 0"] + memory_baseline
        switches = re.search(rb"Preempcoes: (\d+)", text)
        if not switches or int(switches[1]) < 6: raise RuntimeError(f"Preempcao nao confirmada; consulte {log}")
    if scenario in ("user", "user-faults"):
        required += [b"Segmentos Ring 3 prontos: CS=0x2b SS=0x33", b"Alocacoes ativas: 0"] + memory_baseline
        if scenario == "user":
            required += [b"Ring 3: CPL/IOPL, TSS, CR3 privados, dados isolados, PIT, INT80 e limpeza OK."]
        else:
            required += [b"User faults: 26 tentativas bloqueadas, vitima preservada, kernel ativo e memoria restaurada OK."]
            names = ("kernel-read", "kernel-write", "hhdm-read", "hhdm-write", "kernel-exec", "code-write", "data-nx",
                     "null", "stack-low", "stack-high", "stack-nx", "cli", "hlt", "port", "int-gate", "divide",
                     "opcode-df", "fpu", "syscall", "cr3", "msr", "iret-ring0", "invalid-rsp", "invalid-rsp-return", "invalid-rsp-irq")
            for name in names:
                if not re.search(rb"User fault: " + name.encode() + rb" vetor=\d+ erro=0x[0-9a-f]+ OK; recursos restaurados\.", text):
                    raise RuntimeError(f"Probe {name} ausente; consulte {log}")
            if len(re.findall(rb"\[user\] Processo encerrado:", text)) != 26:
                raise RuntimeError(f"Contagem de excecoes de usuario invalida; consulte {log}")
        ids = re.findall(rb"Processo Ring3 criado: (\d+)", text)
        if len(ids) != 1: raise RuntimeError(f"spawnuser invalido; consulte {log}")
        required += [b"Processo coletado: " + ids[0] + b" status=42"]
    if scenario == "syscalls":
        required += [b"Syscalls: programa C CPL3, write, exit/getpid/yield, sleep bloqueante, GPRs/flags e buffers OK.",
                     b"SYS-RO-TEXT", b"SYS-STDERR", b"SYS-BIN:\x00Z", b"X-PAGE", b"SYS-WRITE-SLEEP-ABI-OK",
                     b"Programa de syscalls: status=0", b"Alocacoes ativas: 0"] + memory_baseline
        if b"FORBIDDEN-PARTIAL" in text or b"[FATAL]" in text:
            raise RuntimeError(f"Syscall vazou buffer parcial ou causou panic; consulte {log}")
    if scenario == "sysread":
        required += [b"Sys_read: buffers RW, teclas PS/2, leituras curtas, bloqueio/despertar e limpeza OK.",
                     b"Sys_read: encerramento durante INPUT e limpeza de recursos OK.",
                     b"Sys_read bloqueado: fase=1;", b"Sys_read bloqueado: fase=2;", b"SYS-READ:abc\r\n",
                     b"SYS-READ-MAX:z", b"SYS-READ-OK", b"Programa de syscalls: status=0",
                     b"Ring 3 recebeu: ola", b"Sysdemo concluido: status=0", b"Alocacoes ativas: 0"] + memory_baseline
        if read_phase != 2 or b"[FATAL]" in text:
            raise RuntimeError(f"Entrada de sys_read incompleta; consulte {log}")
    if scenario == "elf":
        required += [b"Modulo pronto: alpha ELF64", b"Modulo pronto: beta ELF64", b"Programas ELF64 (modulos Limine)",
                     b"ELF alpha: inicio CPL3, data/BSS OK", b"ELF beta: inicio CPL3, data/BSS OK",
                     b"ELF alpha: fim isolado", b"ELF beta: fim isolado", b"ELF demo: alpha=0 beta=0",
                     b"Uso: run NOME (um programa).", b"Programa nao encontrado. Digite programs.",
                     b"Alocacoes ativas: 0", b"ELF: arquivos invalidos sem PID, limites e rollback de pilha kernel OK.",
                     b"ELF: dois arquivos, CR3/frames privados, RX/RO/RW-NX, argv/auxv, BSS e exit OK.",
                     b"ELF: 9 protecoes na CPU, kernel ativo e memoria restaurada OK."] + memory_baseline
        for name in ("text-write", "ro-write", "data-nx", "ro-nx", "stack-nx", "kernel-read", "null", "stack-low", "stack-high"):
            if not re.search(rb"ELF fault: " + name.encode() + rb" erro=0x[0-9a-f]+ OK; recursos restaurados\.", text):
                raise RuntimeError(f"Protecao ELF {name} ausente; consulte {log}")
        starts = [text.find(b"ELF alpha: inicio CPL3"), text.find(b"ELF beta: inicio CPL3")]
        finishes = [text.find(b"ELF alpha: fim isolado"), text.find(b"ELF beta: fim isolado")]
        if min(starts) < 0 or min(finishes) < max(starts):
            raise RuntimeError(f"Programas ELF nao executaram simultaneamente; consulte {log}")
        if b"[FATAL]" in text: raise RuntimeError(f"ELF causou panic; consulte {log}")
    if scenario == "diagnostics":
        required += [b"Diagnostico: 3 ciclos de fault/recuperacao/kill sem vazamentos OK.",
                     b"Top: amostra concluida.", b"PID ESTADO CPU% TICKS MODO NOME", b"Uso: top (sem argumentos).",
                     b"Processos preemptivos", b"Uso: kill PID (inteiro positivo).", b"ELF demo: alpha=0 beta=0",
                     b"Alocacoes ativas: 0"] + memory_baseline
        for cycle in range(1,4):
            required += [f"Diagnostico: ciclo={cycle} fault=-270 sobrevivente=0 recuperacao=0 kill=-9 memoria=OK".encode()]
        workers = re.findall(rb"Processo ELF criado: (\d+) \(busy\)", text)
        if len(workers) != 2 or workers[0] == workers[1]: raise RuntimeError(f"Workers CPU invalidos; consulte {log}")
        for pid in workers:
            match = re.search(rb"(?:^|\n)" + pid + rb" READY (\d+)\.(\d)% (\d+) R3 busy\r?\n", text)
            if not match or int(match[1])*10 + int(match[2]) == 0 or int(match[3]) == 0:
                raise RuntimeError(f"Top nao mediu CPU do PID {pid!r}; consulte {log}")
            required += [b"Processo coletado: " + pid + b" status=-9"]
        faults = re.findall(rb"Processo ELF criado: (\d+) \(fault\)", text)
        if len(faults) != 1: raise RuntimeError(f"run fault falhou; consulte {log}")
        required += [b"Processo coletado: " + faults[0] + b" status=-270"]
        if b"[FATAL]" in text: raise RuntimeError(f"Falha de app derrubou kernel; consulte {log}")
    if any(message not in text for message in required):
        raise RuntimeError(f"Diagnostico incompleto; consulte {log}")
    print(f"OK: {scenario}/{mode}, serial e framebuffer; log: {log}", flush=True)
    if scenario == "memory": return expected_bytes


def main():
    parser = argparse.ArgumentParser()
    group = parser.add_mutually_exclusive_group()
    group.add_argument("--errors", action="store_true", help="Testa logs, panic e assert.")
    group.add_argument("--interrupts", action="store_true", help="Testa tabelas, IRQs e excecoes.")
    group.add_argument("--keyboard", action="store_true", help="Injeta teclas PS/2 pelo QMP.")
    group.add_argument("--terminal", action="store_true", help="Testa comandos e edicao do terminal.")
    group.add_argument("--memory", action="store_true", help="Testa RAM em 128, 512 e 5120 MiB.")
    group.add_argument("--pmm", action="store_true", help="Testa alocacao fisica e paginas acima de 4 GiB.")
    group.add_argument("--vmm", action="store_true", help="Testa mapeamentos virtuais e protecoes na CPU.")
    group.add_argument("--heap", action="store_true", help="Testa heap e devolucao de paginas em BIOS/UEFI.")
    group.add_argument("--access", action="store_true", help="Testa acessos invalidos e diagnosticos do heap/CPU.")
    group.add_argument("--memtest", action="store_true", help="Testa comandos meminfo e memtest repetidos.")
    group.add_argument("--processes", action="store_true", help="Testa processos, contexto, terminal e guarda de pilha.")
    group.add_argument("--scheduler", action="store_true", help="Testa preempcao sem yield, registradores e concorrencia.")
    group.add_argument("--user", action="store_true", help="Testa Ring 3, CR3 privados, TSS e INT80.")
    group.add_argument("--syscalls", action="store_true", help="Testa ABI C/Assembly, buffers, write/read/sleep e terminal em CPL3.")
    group.add_argument("--elf", action="store_true", help="Testa programas ELF64 de modulos Limine em CPL3.")
    group.add_argument("--diagnostics", action="store_true", help="Testa ps/kill/run/top, CPU, recuperacao e vazamentos.")
    parser.add_argument("--quick", action="store_true", help="Executa uma configuracao de RAM por cenario, mantendo BIOS/UEFI.")
    args = parser.parse_args()
    Path("build").mkdir(exist_ok=True)
    firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    if not firmware.is_file(): raise SystemExit("OVMF ausente: instale o pacote ovmf.")
    scenarios = (("diagnostics",) if args.diagnostics else ("elf",) if args.elf else ("syscalls", "sysread") if args.syscalls else ("user", "user-faults") if args.user else ("scheduler",) if args.scheduler else ("processes", "process-guard") if args.processes else ("memtest",) if args.memtest else ("access-null", "access-guard-low", "access-guard-high", "access-unmapped", "access-overflow",
                  "access-underflow", "access-freed", "access-doublefree", "access-badfree", "access-nx") if args.access else
                 ("heap",) if args.heap else ("vmm", "vmm-unmapped", "vmm-readonly", "vmm-nx") if args.vmm else
                 ("pmm",) if args.pmm else ("memory",) if args.memory else ("terminal", "terminal-limits") if args.terminal else
                 ("keyboard", "keyboard-echo") if args.keyboard else ("logs", "panic", "assert") if args.errors else
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
            previous = 0
            sizes = (128, 512, 5120) if args.memory or args.pmm or scenario in ("vmm", "heap", "memtest", "processes", "scheduler", "user", "user-faults", "syscalls", "sysread", "elf", "diagnostics") else (256,)
            for size in (sizes[:1] if args.quick else sizes):
                detected = boot(mode, scenario, image, firmware, size)
                if args.memory:
                    if detected <= previous: raise RuntimeError("RAM detectada nao cresceu com a configuracao")
                    previous = detected


if __name__ == "__main__":
    main()
