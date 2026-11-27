# Historico de versoes

## Em desenvolvimento

- Estados canonicos READY/RUNNING/BLOCKED/TERMINATED e motivo separado de bloqueio.
- Transicoes de bloqueio validadas, com despertar somente pelo evento correspondente.

- Gerenciador de processos do kernel com PID, pai e estados de execucao.
- Pilhas RW/NX de 16 KiB com guardas, troca de contexto SysV e round-robin cooperativo.
- Sono pelo PIT, exit/wait, adocao de orfaos, kill e liberacao de pilhas fora da CPU.
- Comandos ps, spawn, kill PID e wait PID; terminal cede CPU ao aguardar teclado.
- Testes de estados/PIDs com ASan/UBSan e contexto/guardas/limpeza em BIOS/UEFI.

## 0.2 - 2026-11-27

- Total agregado de RAM identificada, com uniao de intervalos e exclusao de MMIO.
- Comandos meminfo (alias de memory) e memtest (teste limitado de PMM/VMM/heap).
- Testes de repeticao do memtest, limpeza e limite virtual do heap.

- Diagnostico de page fault com tipo de acesso, causa, bits da CPU e regiao.
- Guardas ausentes antes/depois das arenas e redzones por bloco no heap.
- Auditoria de metadados, veneno de blocos livres e panic 10 para corrupcao.
- Comando heap verifica integridade; dez falhas reais testadas em BIOS/UEFI.

- Heap do kernel com kmalloc/kcalloc/krealloc/kfree e alinhamento de 16 bytes.
- Arenas sob demanda em paginas RW/NX, divisao/fusao e devolucao ao PMM/VMM.
- Rollback de mapeamentos parciais, verificacao de overflow e comando heap.
- Testes ASan/UBSan, estresse e validacao BIOS/UEFI de 128 MiB a 5 GiB.

- VMM de quatro niveis com PML4 propria, janela privada e mapas do boot preservados.
- APIs de map/unmap/protect/translate, aliases e integracao com o PMM.
- NX, CR0.WP, invalidacao de TLB e rollback de tabelas em falta de memoria.
- Comando vmm e testes de page faults reais por ausencia, escrita e execucao NX.

- Alocador fisico de paginas de 4 KiB com bitmap compacto e metadados protegidos.
- Alocacao individual, pagina zerada e blocos contiguos com alinhamento.
- Liberacao validada, contadores atuais e comando pmm no terminal.
- Testes de esgotamento, reutilizacao, reservas e escrita em RAM acima de 4 GiB.

- Deteccao e copia do mapa fisico Limine, com validacao e contagem em 64 bits.
- Comandos memory/memmap e resumo da RAM utilizavel em sysinfo e no boot.
- Contagem de paginas completas, maior bloco utilizavel e categorias reservadas.
- Testes de mapas invalidos e de RAM de 128 MiB a 5 GiB em BIOS/UEFI.

- Corrigida a execucao interativa no QEMU para permitir reboot sem fechar a VM.

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
