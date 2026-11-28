# Historico de versoes

## Em desenvolvimento — auditoria de fundamentos

- Driver ATA PIO com descoberta PCI IDE, IDENTIFY, LBA28/LBA48, leitura/escrita, flush e timeout; comando `disks`.
- Testes de armazenamento em discos descartaveis: BIOS/UEFI, multiplos discos, LBA alto, persistencia e erros de I/O com blkdebug.

- Camada generica de blocos com geometria, LBA/buffers validados, flush e progresso parcial.

- Runtime freestanding completo para memoria/strings e panic com todos os GPRs/CS/SS.
- Framebuffer com limites explicitos, formatos RGB sem mascaras sobrepostas e testes de padding/rolagem.
- GDB real em BIOS/UEFI, stack ABI e probes de inicializacao DATA/BSS.
- Testes de resolucoes, respostas Limine ausentes, mascaras/IRQs espurias, timer/idle e teclado/historico.
- Reboots repetidos na mesma VM e harness de soak com duracao real, sem simular uma hora.

## 0.3 - 2026-11-28

- Build 0.3 e alvo check-release com suites ASan/UBSan/QEMU e verificacao SHA-256 da ISO.

- Diagnostico de ps/kill/run/top com dois ELFs CPU-bound e recuperacao apos page fault.
- Tres ciclos de app com falha, sobrevivente, nova execucao e kill com PMM/VMM/heap/PCB/TCB restaurados.
- Modulos busy/fault para experimentar CPU e encerramento seguro no terminal.

- top basico: amostra de CPU por PID durante um segundo, ordenada por uso, incluindo Idle.
- Percentuais sem overflow, PID independente de slot e testes ASan/UBSan do monitor.

- Validacao QEMU de dois ELFs simultaneos com CR3/frames/argv independentes e limpeza integral.
- Nove probes ELF na CPU verificam RX/RO/NX, memoria do kernel, pagina zero e guardas.
- Rejeicao sem consumir PID, rollback da pilha kernel e teste dos comandos run/wait.

- Criacao de processos ELF com CR3 proprio, entry do arquivo e stack inicial em CPL3.
- Modulos Limine alpha/beta na ISO, tres PT_LOAD por programa e dados/BSS independentes.
- Comandos programs, run NOME e elfdemo para executar dois programas em conjunto.

- Nucleo ELF64 estatico com validacao de PT_LOAD, intervalos, alinhamentos, entry e W^X.
- Paginas privadas RX/RO/RW-NX, BSS zerada, stack/argv/auxv e rollback integral em OOM.
- Testes ASan/UBSan de arquivos hostis e 10000 mutacoes deterministicas.

- read bloqueante no PS/2 com copyout RW, leituras curtas e erros sem consumir entrada.
- Erro ENODEV antes de inicializar o teclado; teste de kill enquanto espera entrada.
- Comando sysdemo executa programa C interativo em Ring 3; testes com teclas pelo QMP.
- Teste de spawnuser aguarda a mensagem completa antes de capturar o PID da serial.

- Dispatcher INT80 preemptivel em contexto de tarefa; exit/getpid/yield e sleep em milissegundos.
- write com stdout/stderr, copia segura, tamanho limitado e saida binaria na serial.
- Programa C freestanding em CPL3 testa erros, buffers, sono bloqueante e GPRs/flags.

- ABI v1 das syscalls em cabecalho compartilhado com programas C e constantes NASM geradas.
- Validacao integral de buffers USER, permissoes de escrita, limites e overflow antes da copia.

- Excecoes em CPL3 encerram somente o PID culpado; CPL0 e falhas criticas continuam fatais.
- 26 probes reais de protecao, incluindo kernel/HHDM, RX/NX, I/O, Ring 0 e pilha invalida.
- Validacao de seletores, RIP/RSP e flags antes do retorno a Ring 3, apos EOI nas IRQs.
- Isolamento entre PIDs, paginas zeradas e recursos restaurados apos exit/fault.

- Execucao real em Ring 3 por IRETQ, entrada INT80 e troca de CR3 por processo.
- Chamadas exit/yield/getpid/gettid/ticks; comando spawnuser e exibicao R0/R3 em ps.
- Teste de dois processos no mesmo endereco virtual com dados distintos e limpeza integral.

- Espacos privados de quatro niveis, paginas USER/RX/RW-NX e metade alta supervisor compartilhada.
- Construcao limitada, copia pelo loader, rollback OOM e destruicao de espacos inativos.

- Segmentos de codigo/dados Ring 3 na GDT e RSP0 do TSS atualizado por thread.

- Round Robin preemptivo com fila FIFO e quantum de 5 ticks/50 ms no PIT de 100 Hz.
- Troca apos EOI, preservando GPRs, pilha e RFLAGS pelo frame IRQ/IRETQ.
- Suspensao/retomada de threads e adiamento aninhado de preempcao.
- Protecao de estruturas, logs, console, serial e sequencias de energia.
- Testes sem yield em Assembly, flags, concorrencia de memoria e filas com ASan/UBSan.

- Processo Idle PID/TID 0, com PCB/TCB e pilha protegida permanentes.
- Idle so executa sem trabalho pronto; teclado bloqueia leitores e os acorda por IRQ1.
- Baseline de memoria inclui os recursos permanentes do Idle e e validado nas suites.

- Recursos associados ao PID: heap automatico, blocos fisicos, mapas virtuais e destrutores.
- Limpeza de grupos apos sair da CPU, transferencia de posse e rollback quando o registro enche.

- PCBs separados de TCBs, PIDs/TIDs independentes e multiplas threads por processo.
- Criacao/join/exit de threads, deteccao de ciclos de join e encerramento de grupos.
- Comando threads; ps agrega estado e contadores do processo.

- Estados canonicos READY/RUNNING/BLOCKED/TERMINATED e motivo separado de bloqueio.
- Transicoes de bloqueio validadas, com despertar somente pelo evento correspondente.

- Gerenciador de processos do kernel com PID, pai e estados de execucao.
- Pilhas RW/NX de 16 KiB com guardas e base de troca de contexto SysV.
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
