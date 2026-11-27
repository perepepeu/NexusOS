# Historico de versoes

## 0.1 - 2026-11-26

- Comandos reboot e shutdown com reset ACPI/PS2 e desligamento ACPI S5.
- Validacao das tabelas ACPI e leitor limitado de pacotes AML constantes.
- Testes de eventos reais de energia em BIOS/UEFI, fallback e ACPI ausente.

- Terminal com comandos help, clear, echo, version, uptime, sysinfo e history.
- Editor com cursor, insercao, historico de oito entradas e atalhos Ctrl.
- Rolagem vertical do console e horizontal da linha de entrada.
- Testes de comandos e limites pelo teclado virtual em BIOS e UEFI.

- Driver PS/2 por IRQ1, fila de eventos, modificadores e traducao ASCII US.
- Demonstracao de entrada com eco, Backspace, Tab e envio de linhas por Enter.
- Testes de teclado com injecao de teclas pelo QMP em BIOS e UEFI.

- GDT propria, TSS e pilhas IST para double fault e NMI.
- IDT de 256 vetores, stubs Assembly e diagnostico de excecoes da CPU.
- PIC remapeado, PIT a 100 Hz e encaminhamento ExtINT pelo LAPIC.
- Espera com interrupcoes ativas e contador de ticks.
- Autotestes de tabelas, retorno de INT3, registros e falhas reais em BIOS/UEFI.

## 0.0.1 - 2026-11-26

Primeira versao do NexusOS.

- Ambiente de compilacao C e Assembly no Ubuntu/WSL.
- Kernel inicial x86_64, com entrada Assembly e codigo C freestanding.
- Boot com Limine em BIOS e UEFI, testado no QEMU.
- Console no framebuffer e saida serial.
- Logs com niveis, modulo e sequencia.
- Panic com codigo, arquivo e linha, alem de assertions do kernel.
- Testes de boot, logs, erros recuperaveis, panic e assert em BIOS e UEFI.

Ainda sem shell, teclado, gerenciamento de memoria ou handlers de excecoes
da CPU. Ao concluir a inicializacao, o kernel aguarda com a CPU parada.
