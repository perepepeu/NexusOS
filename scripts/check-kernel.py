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


def type_terminal_test(monitor_path, limits=False, memory=False, pmm=False, vmm=False, heap=False, memtest=False, processes=False):
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

    if processes:
        command("ps")
        command("spawn extra")
        command("spawn")
        command("wait 5")
        time.sleep(5.3)  # Aguarda o filho sem inundar a fila PS/2 enquanto o terminal bloqueia.
        command("spawn")
        command("kill 6")
        command("wait 6")
        command("wait 6")
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
    if scenario in ("memory", "pmm", "vmm", "heap", "memtest", "processes"): suffix = f"{scenario}-{ram_mib}-{mode}"
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
                deadline = time.monotonic() + (50 if scenario == "processes" else 40 if scenario == "pmm" else 20)
                injected = False
                while marker not in log.read_bytes():
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise RuntimeError(f"Kernel nao concluiu {scenario}/{mode}; consulte {log}")
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
                        type_terminal_test(monitor_path, processes=True)
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
                or values["alocadas"] != 1 or values["de metadados"] == 0):
            raise RuntimeError(f"Contadores PMM incorretos; consulte {log}")
        bitmap = re.search(rb"Bitmap fisico: 0x([0-9a-f]+)", text)
        if not bitmap or int(bitmap[1], 16) == 0 or int(bitmap[1], 16) % 4096:
            raise RuntimeError(f"Bitmap PMM invalido; consulte {log}")
    if scenario == "vmm":
        required += [b"VMM: CR3, aliases, isolamento, permissoes e limpeza OK.",
                     b"Memoria virtual do kernel - 4 niveis", b"Paginas mapeadas: 0",
                     b"Tabelas proprias: 1 paginas", b"Paginas alocadas: 1"]
        if ram_mib > 4096: required += [b"VMM: frame acima de 4 GiB mapeado e acessado OK."]
    if scenario == "heap":
        required += [b"Heap: alinhamento, zero, realloc, fusao e devolucao ao PMM/VMM OK.",
                     b"Integridade do heap: OK.",
                     b"Heap do kernel - alinhamento 16 bytes", b"Alocacoes ativas: 0",
                     b"Arenas ativas: 0", b"Bytes solicitados: 0 bytes", b"Bytes mapeados: 0 bytes",
                     b"Paginas mapeadas: 0", b"Tabelas proprias: 1 paginas", b"Paginas alocadas: 1"]
    if scenario == "memtest":
        required += [b"RAM total identificada:", b"Uso: memtest (sem argumentos).", b"Integridade do heap: OK.",
                     b"Paginas alocadas: 1", b"Paginas mapeadas: 0", b"Tabelas proprias: 1 paginas",
                     b"Alocacoes ativas: 0", b"Arenas ativas: 0", b"Bytes mapeados: 0 bytes"]
        if text.count(b"Memtest: PMM, VMM e heap OK; sem vazamentos.") != 2 or b"Memtest: FALHOU" in text:
            raise RuntimeError(f"Memtest falhou/repeticao deixou vazamentos; consulte {log}")
    if scenario == "processes":
        required += [b"Processos: contexto, round-robin, sono, wait, kill, guardas e limpeza OK.",
                     b"Processos do kernel (cooperativos, ring 0)", b"PID PPID ESTADO TICKS SAIDA NOME",
                     b"Uso: spawn (sem argumentos).", b"Processo criado: 5", b"Processo criado: 6",
                     b"Processo coletado: 5 status=0", b"Processo encerrado: 6", b"Processo coletado: 6 status=-9",
                     b"Filho inexistente ou ja coletado.", b"Processo inexistente, encerrado ou protegido.",
                     b"Uso: kill PID (inteiro positivo).", b"Paginas alocadas: 1", b"Paginas mapeadas: 0",
                     b"Tabelas proprias: 1 paginas", b"Alocacoes ativas: 0"]
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
    args = parser.parse_args()
    Path("build").mkdir(exist_ok=True)
    firmware = Path("/usr/share/OVMF/OVMF_CODE_4M.fd")
    if not firmware.is_file(): raise SystemExit("OVMF ausente: instale o pacote ovmf.")
    scenarios = (("processes", "process-guard") if args.processes else ("memtest",) if args.memtest else ("access-null", "access-guard-low", "access-guard-high", "access-unmapped", "access-overflow",
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
            for size in ((128, 512, 5120) if args.memory or args.pmm or scenario in ("vmm", "heap", "memtest", "processes") else (256,)):
                detected = boot(mode, scenario, image, firmware, size)
                if args.memory:
                    if detected <= previous: raise RuntimeError("RAM detectada nao cresceu com a configuracao")
                    previous = detected


if __name__ == "__main__":
    main()
