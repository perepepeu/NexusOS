# NexusOS

Versao atual: **0.3**. A versao fica registrada em `VERSION` e na tag Git
`v0.3`. Veja as entregas em [CHANGELOG.md](CHANGELOG.md).

Download: [Release v0.3](https://github.com/perepepeu/NexusOS/releases/tag/v0.3).

Em desenvolvimento apos v0.3: [driver de armazenamento ATA PIO](docs/storage.md).
Use `disks` no terminal para listar os dispositivos e `make check-storage` no
WSL para testar leitura/escrita em discos virtuais descartaveis.

O [VFS com RAMFS](docs/vfs.md) fornece caminhos, montagens e descritores por
processo. Testes: `make check-vfs`. No NexusOS, experimente `ls /`,
`cat /etc/motd`, `mkdir /tmp/docs`, `write /tmp/docs/nota ola` e
`cat /tmp/docs/nota`. Os arquivos do RAMFS existem somente na memoria.

Os modulos de inicializacao do Limine aparecem em `/boot/modules`, somente
para leitura. Use `ls /boot/modules` no NexusOS e `make check-ramfs` no WSL.
Veja [RAMFS e inicializacao](docs/ramfs.md).

[FAT32 em modo de leitura](docs/fat32.md): `mountfat ata0 /mnt`, `ls /mnt`,
`cat /mnt/ARQUIVO.TXT`, `fatinfo` e `umount /mnt`. Testes descartaveis:
`make check-fat32` (requer mtools no WSL).

Para [escrita FAT32](docs/fat32-write.md), use `mountfatrw ata0 /mnt`.
Estao disponiveis `write`, `append`, `mkdir`, `truncate`, `mv`, `rm`, `rmdir`
e `sync`. Testes: `make check-fat32-write` (mtools e dosfstools).

## Compilador C e Assembly

Ambiente: Ubuntu 24.04 no WSL 2, com GCC 13, NASM 2.16 e GNU Binutils.
Os comandos abaixo devem ser executados no Ubuntu, na pasta do projeto:

```sh
cd /mnt/c/Users/HP/Desktop/NexusOS
make check-toolchain
make check-toolchain ARCH=i686
```

O primeiro comando verifica x86 de 64 bits; o segundo verifica x86 de 32 bits.
O kernel inicial usa x86_64; i686 permanece apenas como teste do compilador.

As opcoes ficam em `toolchain.mk`: C freestanding, sem PIE, sem protetor de
pilha dependente do runtime e sem bibliotecas do Linux. O NASM produz objetos
ELF compativeis com o linker. Em 64 bits, a red zone fica desativada.

A verificacao compila C e Assembly e gera um ELF temporario com uma chamada
entre os dois. Isso valida a compilacao e vinculacao; ainda nao inicia um OS.

Usamos o GCC instalado no Ubuntu com essas opcoes. Ele ainda nao e um
compilador cruzado dedicado `x86_64-elf-gcc` ou `i686-elf-gcc`.

## QEMU e Limine

No Ubuntu/WSL, instale as dependencias caso esteja preparando outra maquina:

```sh
sudo apt-get update
sudo apt-get install build-essential nasm xorriso curl python3 gdb qemu-system-x86 qemu-system-gui ovmf
make setup-limine
make check-kernel
```

Limine 12.11.1 e baixado da release oficial, com versao e SHA-256 fixos no
script `scripts/setup-limine.sh`. Seus binarios e licencas ficam em
`vendor/limine`, fora do Git. A ferramenta de instalacao BIOS e compilada no WSL.

`make` ou `make iso` gera `build/nexusos.iso`, com boot BIOS e UEFI x86_64.
A imagem inclui o kernel em `/boot/kernel.elf`, conforme `boot/limine.conf`.
O Limine inicia o kernel automaticamente apos 3 segundos; Enter inicia antes.

Para iniciar em uma janela grafica pelo WSLg, com serial no terminal:

```sh
make run       # BIOS
make run-uefi  # UEFI com OVMF
```

Use Ctrl+A, depois X, no terminal para sair do QEMU, ou feche sua janela.
Para executar apenas pela serial, sem WSLg, use `make run DISPLAY_BACKEND=none`
ou `make run-uefi DISPLAY_BACKEND=none`.
Esses comandos permitem reiniciar a VM. Ao executar o QEMU manualmente, omita
`-no-reboot`: essa opcao encerra o processo quando o guest solicita um reset.
Os comandos usam emulacao TCG, sem depender de permissoes KVM.
O teste `make check-kernel` inicia as duas variantes sem interface grafica,
verifica a resposta do protocolo Limine e a mensagem final do kernel na serial,
e confirma que a VM permanece ativa apos a mensagem final. Cada boot tem um
limite de 20s para apresentar o resultado.
O teste captura a tela via QMP e verifica os pixels de fundo e texto do kernel.
Logs ficam em `build/boot-bios.log` e `build/boot-uefi.log`; capturas ficam em
`build/screen-bios.ppm` e `build/screen-uefi.ppm`.
Os comandos anteriores `run-bootloader`, `run-bootloader-uefi`, `bootloader-iso`
e `check-bootloader` continuam disponiveis como aliases para a imagem completa.
A imagem antiga `build/nexusos-bootloader.iso` nao e mais utilizada.

## Kernel inicial

`make kernel` compila `build/kernel.elf` sem bibliotecas ou runtime do Linux.
O kernel e carregado no endereco virtual `0xffffffff80000000`, com segmentos
separados para codigo, constantes e dados. A entrada Assembly configura uma
pilha de 16 KiB alinhada a 16 bytes e chama `kmain` em C.

O kernel valida a revisao base 6 do protocolo Limine, consulta a identificacao
do bootloader e escreve pela porta serial COM1 a 115200 baud:

```text
NexusOS

[2] [INFO] [video] Tela: framebuffer pronto.
[3] [INFO] [boot] Bootloader: Limine 12.11.1
[4] [INFO] [boot] Protocolo Limine: OK
...
[11] [INFO] [boot] NexusOS: kernel inicial pronto. CPU em espera.
```

Ao terminar, a CPU aguarda com `sti; hlt`, acordando nas interrupcoes do timer.
Falhas fatais usam `cli; hlt`. Apos o boot, um terminal executa comandos internos.
A saida do kernel
aparece na tela e no terminal serial dos comandos `make run` e `make run-uefi`.
O header oficial do protocolo e versionado em `kernel/include/limine.h`, com
licenca e commit de origem preservados. Falhas no protocolo imprimem PANIC e
interrompem a inicializacao.

## Mensagens na tela

O console usa o framebuffer RGB de 32 bits entregue pelo Limine, respeitando
o pitch e as mascaras de cor. Uma fonte propria 5x7 ampliada 2x desenha letras,
numeros e pontuacao basica sobre fundo escuro. Letras sao exibidas em maiusculas;
caracteres nao suportados usam `?`. O console suporta quebra de linha e ajuste
automatico ao limite horizontal. Ao esgotar as linhas, rola o conteudo uma
linha para cima. Se nao houver framebuffer compativel, a inicializacao continua pela
serial com um aviso.

## Logs e tratamento de erros

`kernel/log.c` centraliza a saida na serial e no console. Cada registro tem
um numero sequencial, nivel e modulo. O primeiro registro de boot aparece
apenas na serial, antes da inicializacao da tela. Os niveis sao `DEBUG`,
`INFO`, `WARN`, `ERROR` e `FATAL`; o filtro padrao e `INFO`.

```c
#include <log.h>
#include <panic.h>

log_info("memoria", "Paginas livres: %u", 128u);
log_warn("video", "Modo opcional indisponivel; usando alternativa.");
log_error("driver", "Operacao falhou: %x", 0x10u);
KASSERT(ponteiro != 0);
```

O formatador sem libc suporta `%s`, `%u` e `%x` para `unsigned int`, `%llu` para
`unsigned long long` (64 bits) e `%p` para
`void *` e `%%`. Strings nulas aparecem como `(null)`. `log_set_level` altera o
filtro; `cmdline: log=debug` na entrada Limine habilita diagnostico detalhado.
Avisos e erros recuperaveis registram a falha e retornam ao chamador.
O chamador continua responsavel por escolher uma alternativa ou propagar o erro.

`PANIC(codigo, mensagem)` registra uma falha fatal com arquivo e linha. A tela
e limpa para destacar o diagnostico; o historico permanece na serial. A CPU
para com interrupcoes desabilitadas, mantendo a mensagem visivel. `KASSERT`
permanece ativo em todas as compilacoes e encaminha falhas ao mesmo handler.
Uma reentrada no handler usa uma mensagem minima na serial.

| Codigo | Erro |
| --- | --- |
| 1 | Revisao do protocolo Limine incompativel |
| 2 | Identificacao do bootloader ausente |
| 3 | Falha fatal injetada pelo autoteste |
| 4 | Assert do kernel falhou |
| 5 | Excecao da CPU ou interrupcao inesperada |
| 6 | Mapa de memoria fisica ausente ou invalido |
| 7 | Falha ao inicializar o alocador de paginas fisicas |
| 8 | Falha ao inicializar a memoria virtual |
| 9 | Falha ao inicializar o heap |
| 10 | Ponteiro invalido ou corrupcao detectada no heap |
| 11 | Falha no gerenciador de processos |

```sh
make check-kernel  # Boot normal, BIOS e UEFI
make check-errors  # Logs, erro recuperavel, panic e assert, BIOS e UEFI
```

Os testes de erro geram ISOs separadas com `selftest=logs`, `selftest=panic`
ou `selftest=assert` na linha de comando do kernel. A ISO normal permanece
sem injecao de falhas. Os testes verificam niveis, filtro, formatacao, sequencia,
arquivo/linha, continuidade apos erro recuperavel e parada apos erro fatal.
Logs e capturas ficam em `build/boot-{cenario}-{bios|uefi}.log` e
`build/screen-{cenario}-{bios|uefi}.ppm`.

Os registros sao emitidos imediatamente, sem arquivo de log ou buffer persistente.
O logger opera em uma CPU. O handler do timer nao escreve logs, evitando
reentrada enquanto o kernel escreve. Handlers de excecoes executam com
interrupcoes mascaraveis desabilitadas.

## GDT, IDT e interrupcoes

A GDT propria contem segmentos ring 0 de codigo (`0x08`), dados (`0x10`) e
um TSS de 64 bits (`0x18`). O kernel recarrega os seletores e TR. O TSS reserva
pilhas de 16 KiB independentes para double fault (IST1), NMI (IST2) e RSP0.
Ha tambem segmentos DPL3 (`0x2b`/`0x33`); o escalonador atualiza RSP0 para a
pilha de kernel da thread ativa. Veja Kernel Mode e User Mode abaixo.

A IDT contem 256 interrupt gates; apenas INT80 permite invocacao por Ring 3. Os stubs
Assembly normalizam o codigo de erro, preservam os 15 registradores gerais,
limpam DF para chamar C e alinham a pilha ao ABI. O retorno usa `iretq` para
restaurar o contexto e RFLAGS. `INT3` registra o breakpoint e retorna; as demais
excecoes em CPL0 param o kernel com nome, vetor, codigo de erro, RIP, RSP e RFLAGS.
Excecoes de processos CPL3 encerram o processo, preservando o kernel.
Page fault tambem informa CR2. Double fault usa IST1 e verifica a pilha utilizada.

O PIC 8259 e remapeado para os vetores `32-47`. A inicializacao libera IRQ0;
o driver de teclado libera IRQ1 quando conclui sua configuracao.
O PIT gera aproximadamente 100 interrupcoes por segundo. O handler incrementa
um contador de ticks e envia EOI; IRQ7 e IRQ15 espurias recebem o tratamento
especifico do PIC. O LAPIC encaminha ExtINT para o PIC em modo virtual wire,
com vetor espurio `255`. Essa etapa usa uma CPU e o PIC; nao configura IOAPIC,
roteamento SMP ou scheduler.

Para acessar o LAPIC xAPIC, o kernel mapeia uma unica pagina MMIO de 4 KiB em
`0xfffffe0000000000`, com PCD/PWT. As tabelas auxiliares sao estaticas; enderecos
fisicos e HHDM sao obtidos do Limine. Esse caminho exige paging de quatro niveis
e o slot virtual livre; nao substitui um gerenciador geral de memoria. O caminho
x2APIC usa MSRs quando esse modo ja esta ativo.

```sh
make check-interrupts
```

O teste executa seis cenarios em BIOS e UEFI: leitura de GDTR/IDTR/TR e
seletores, retorno de INT3 com preservacao de registros e DF, repeticao de IRQ0,
divisao por zero, opcode invalido, page fault, general protection e double fault
real com verificacao da IST. As falhas reais verificam vetor, codigo de erro e
contexto da CPU, alem da saida na serial e dos pixels na tela.

As ISOs de teste usam `selftest=interrupts`, `selftest=divide`, `selftest=opcode`,
`selftest=pagefault`, `selftest=protection` e `selftest=doublefault`. O boot normal
aguarda alguns ticks antes de informar que o kernel esta pronto.

Referencia de arquitetura: [Intel SDM, volumes de programacao de sistema](https://cdrdv2-public.intel.com/874240/325462-090-sdm-vol-1-2abcd-3abcd-4.pdf).

## Driver de teclado PS/2

O driver configura o controlador i8042 nas portas `0x60/0x64`, negocia ACKs
com ate tres tentativas e limites de espera, seleciona scancodes set 2 no
dispositivo e habilita traducao para set 1 no controlador. IRQ1 usa o vetor
33 da IDT. Se a inicializacao falhar, a IRQ permanece mascarada e o kernel
continua com um aviso.

O handler apenas le os bytes, atualiza o estado e enfileira eventos; nao imprime
nem espera por comandos dentro da interrupcao. A fila comporta 63 eventos;
quando cheia, descarta novos eventos e contabiliza os descartes. O consumo
protege os indices com interrupcoes desabilitadas. `keyboard_wait` exige IF
habilitado e usa `sti; hlt` para aguardar sem perder o wakeup entre a consulta
da fila e a espera.

```c
#include <keyboard.h>

struct key_event event;
if (keyboard_read(&event) && event.pressed) {
    // event.keycode, event.character e event.modifiers
}
```

Cada evento informa press/release, scancode sem break bit, caractere ASCII
e flags Shift/Ctrl/Alt/Caps Lock. O bit 8 do keycode marca teclas E0, incluindo
setas e modificadores direitos. Os shifts esquerdo e direito sao independentes;
Caps Lock combina com Shift apenas nas letras. O layout inicial e **US**.
Pause e Print Screen sao ignorados sem alterar os modificadores. Ainda nao ha
layout ABNT2, acentos, USB HID, LEDs ou traducao dos digitos do teclado numerico.

Ao iniciar com `make run`, clique na janela do QEMU e digite no terminal `nexus>`.
Uma demonstracao separada do driver, `teclado>`, permanece nas ISOs de teste:
ela ecoa caracteres, aceita Backspace, Tab e Enter, sem executar comandos.
A fonte da tela
continua exibindo letras em maiusculas; a serial preserva o caractere original.

```sh
make check-keyboard
```

O teste injeta teclas no dispositivo PS/2 real do QEMU via QMP, em BIOS e UEFI.
Verifica press/release, Shift, Caps Lock, Shift+Caps, pontuacao, Backspace,
Enter, Ctrl/Alt direitos e esquerdos, seta E0 e os dois shifts simultaneos.
As linhas finais e os eventos sao conferidos na serial; uma captura verifica
os pixels da tela. O teste tambem repete a digitacao sem trace para conferir
a demonstracao isolada do driver. `selftest=keyboard` habilita o trace
de eventos apenas na ISO de teste. Logs: `build/boot-keyboard-{bios|uefi}.log`
e `build/boot-keyboard-echo-{bios|uefi}.log`.

## Terminal basico

O prompt `nexus>` executa comandos internos do kernel, sem programas externos
ou sistema de arquivos. A entrada usa o teclado PS/2; a serial espelha a saida
e o editor usa sequencias ANSI nessa saida. O terminal nao recebe comandos pela
entrada serial. Comandos ignoram maiusculas/minusculas; argumentos preservam
o texto digitado, sem interpretacao de aspas, pipes ou redirecionamentos.

| Comando | Resultado |
| --- | --- |
| `help` | Lista comandos e atalhos |
| `clear` | Limpa a tela e a saida do terminal serial |
| `echo texto` | Exibe o argumento, que pode estar vazio |
| `version` | Exibe a versao gerada a partir de `VERSION` |
| `uptime` | Tempo desde a configuracao do PIT, com centesimos nominais |
| `sysinfo` | Arquitetura, bootloader, timer e estado do teclado |
| `history` | Lista os ultimos oito comandos, incluindo `history` |
| `reboot` | Reinicia o sistema e volta ao boot |
| `shutdown` | Desliga a maquina via ACPI S5 |
| `memory` | Resume a memoria utilizavel e as categorias do mapa fisico |
| `memmap` | Lista enderecos fisicos, tamanhos e tipos de todas as regioes |
| `pmm` | Mostra paginas livres, alocadas e reservadas para o bitmap |
| `vmm` | Mostra CR3, janela virtual e tabelas de paginas proprias |
| `sysdemo` | Executa um programa C em Ring 3 que le uma linha e mostra a resposta |
| `programs` | Lista os programas ELF64 fornecidos pelo Limine |
| `run NOME` | Carrega um ELF64 em segundo plano e mostra seu PID |
| `elfdemo` | Executa alpha e beta em conjunto e aguarda seus resultados |
| `top` | Mede CPU por PID durante um segundo e volta ao prompt |

O editor aceita 127 caracteres, insercao na posicao do cursor, Left/Right,
Home/End, Delete, Backspace e Tab (quatro espacos). Up/Down navegam pelo
historico e restauram o rascunho ao voltar para a entrada atual. Ctrl+A/E
movem ao inicio/fim; Ctrl+U apaga a linha; Ctrl+C cancela; Ctrl+L limpa a tela
preservando o texto em edicao. O cursor e sublinhado, com rolagem horizontal
da linha de entrada e rolagem vertical da saida. O historico vive apenas em RAM.

Comandos inexistentes e argumentos invalidos apresentam uma mensagem e
retornam ao prompt. Entradas alem do limite sao recusadas com aviso. A falta
de eventos deixa a CPU em espera, mantendo as interrupcoes ativas.

```sh
make run
make check-terminal
```

Os testes injetam comandos pelo teclado virtual em BIOS/UEFI e verificam
respostas, versao, uptime, historico circular, restauracao de rascunho,
insercao/Delete/Backspace, cancelamento, clear/Ctrl+L, espacos e limite de
entrada. Capturas ficam em `build/screen-terminal-{bios|uefi}.ppm` e
`build/screen-terminal-limits-{bios|uefi}.ppm`, com logs de mesmos sufixos.

## Reinicializacao e desligamento

Digite `reboot` ou `shutdown` no terminal, sem argumentos. Os comandos operam
no hardware do sistema convidado: no QEMU, reboot reinicia a VM e shutdown
encerra a VM. Se o mecanismo nao estiver disponivel ou falhar, uma mensagem
informa o erro e o terminal continua ativo.

O kernel solicita RSDP e mapa de memoria ao Limine. Antes de acessar ACPI,
valida as regioes mapeadas, assinaturas, tamanhos e checksums de RSDP,
RSDT/XSDT, FADT e DSDT. O reset usa o registro I/O da FADT quando suportado,
com alternativa pelo comando `0xFE` do controlador PS/2. As tentativas possuem
espera limitada e registram o mecanismo utilizado na serial e na tela.

O desligamento descobre os tipos S5 no objeto AML constante `\_S5_` da DSDT,
habilita o modo ACPI via SMI quando necessario e programa SLP_TYP/SLP_EN nos
registradores PM1a/PM1b. Nenhuma porta de desligamento do QEMU e fixada no
codigo: enderecos e valores vem das tabelas do firmware.

O backend atual suporta registros System I/O e um subconjunto constante de
AML. Ele nao e um interpretador ACPI completo: nao executa metodos como
`_PTS`/`_GTS`, nao resolve S5 calculado por metodos/SSDT e nao implementa
hardware-reduced ACPI nem registros de energia em MMIO/PCI. A validacao de
funcionamento foi feita no QEMU q35, com SeaBIOS e OVMF.

```sh
make check-power
```

O teste verifica oito casos: shutdown, reboot ACPI, reboot alternativo PS/2
e shutdown sem ACPI, em BIOS e UEFI. Os comandos sao digitados pelo teclado
virtual; o monitor apenas observa eventos. Shutdown exige evento SHUTDOWN do
guest e encerramento limpo do processo QEMU. Reboot exige evento RESET do
guest, um segundo boot completo e um comando funcionando no novo terminal.
O caso sem ACPI confirma o erro e a continuidade do terminal. Logs e eventos
ficam em `build/power-{cenario}-{bios|uefi}*`.

`make check-acpi` testa o parser AML com ASan/UBSan: pacotes validos, dados
truncados, textos falsos em buffers/metodos e 10000 entradas aleatorias.
O parser ignora corpos que nao interpreta e rejeita tamanhos ou tipos invalidos.

Referencia: [ACPI Hardware Specification](https://uefi.org/specs/ACPI/6.5/04_ACPI_Hardware_Specification.html).

## Deteccao de memoria fisica

O kernel usa o mapa fornecido pelo Limine, sem escrever em enderecos de RAM
para tentar descobri-la. Copia ate 1024 descritores para uma area propria e
valida ponteiros, tamanhos, ordem, overflow e sobreposicoes com RAM utilizavel
ou reclamavel do bootloader. Um mapa ausente ou invalido gera panic de codigo 6.

`memory` exibe os bytes marcados `USABLE`, paginas completas de 4 KiB,
maior bloco contiguo paginado e categorias separadas de bootloader, ACPI,
kernel/modulos, NVS, reservas/MMIO, framebuffer, memoria defeituosa e tipos
desconhecidos. `sysinfo` tambem informa a quantidade utilizavel em MiB.
`memmap` mostra os descritores originais copiados, com enderecos e contagens
de 64 bits. A deteccao e registrada no log de boot.

Somente `USABLE` alimenta o alocador de paginas. Regioes do bootloader e
ACPI permanecem ocupadas: suas estruturas ainda sao usadas pelo kernel.
Tipos desconhecidos e memoria defeituosa nunca entram na contagem utilizavel.
A contagem de paginas arredonda o inicio para cima e o fim para baixo e exclui
a pagina fisica zero. Regioes adjacentes da mesma categoria sao unidas para
essa contagem e para encontrar o maior bloco.

O protocolo permite sobreposicao entre descritores nao utilizaveis. A soma
de cada categoria usa a uniao dos intervalos, evitando duplicatas dentro dela.
Categorias diferentes ainda podem se sobrepor; seus valores nao devem ser
somados para inferir a RAM instalada. Reservas e MMIO podem estar fora da RAM,
e nem o ultimo endereco do mapa nem a soma de todos os descritores representam
capacidade de RAM. Esses valores descrevem o mapa original do boot. A linha
`Livre no alocador` e o comando `pmm` consultam o estado atual do alocador.

```sh
make run
# No terminal NexusOS: memory, memmap ou sysinfo
make check-memory
```

O teste de kernel usa 128, 512 e 5120 MiB no QEMU, em BIOS e UEFI. Recalcula
bytes e paginas a partir da saida de `memmap`, compara com o resumo e verifica
crescimento da memoria detectada e suporte a regioes acima de 4 GiB.
`make check-memory-unit` usa ASan/UBSan para mapas mistos, copia dos dados,
alinhamento, pagina zero, tipos desconhecidos, sobreposicoes legais/ilegais,
entradas nulas/vazias, ordem e overflow. Logs ficam em
`build/boot-memory-{MiB}-{bios|uefi}.log`, com capturas de mesmos sufixos.

Referencia: [Limine Memory Map Feature](https://github.com/Limine-Bootloader/limine-protocol/blob/877c2d4c50e170575f8b8e7fbf7b8fa89b1dc157/PROTOCOL.md#memory-map-feature).

## Alocador de paginas fisicas (PMM)

O PMM inicializa depois da deteccao de memoria e gerencia paginas de 4096 bytes.
Somente paginas completas de regioes `USABLE` entram no pool; a pagina zero,
kernel/modulos, framebuffer, ACPI, bootloader, reservas e tipos desconhecidos
permanecem fora dele. Faixas utilizaveis adjacentes sao unidas.

O bitmap usa um bit por pagina utilizavel, sem representar buracos de MMIO.
Seu tamanho e calculado em 64 bits e suas paginas sao retiradas de uma faixa
utilizavel contigua, acessada pelo HHDM do Limine. Essas paginas sao marcadas
como metadados permanentes e nao podem ser alocadas ou liberadas pelo cliente.
Nao ha limite fixo de RAM em GiB; os limites sao o mapa de 1024 descritores,
o espaco de enderecos e o HHDM fornecido pelo bootloader.

```c
#include <pmm.h>

uint64_t page = pmm_alloc_page();             // Uma pagina, sem zerar.
uint64_t clean = pmm_alloc_zeroed_page();     // Uma pagina de 4096 bytes zerados.
uint64_t block = pmm_alloc_pages(3, 8);       // Tres paginas; inicio alinhado a 8 paginas.
if (page) pmm_free_page(page);
if (clean) pmm_free_page(clean);
if (block) pmm_free_pages(block, 3);
```

As alocacoes retornam **enderecos fisicos**, com zero indicando falha ou falta
de memoria. Alinhamento e medido em paginas e deve ser uma potencia de dois.
Blocos contiguos nunca atravessam lacunas fisicas. A API nao cria novos
mapeamentos virtuais; a versao zerada utiliza o HHDM existente. As demais
alocacoes preservam o conteudo anterior da RAM.

Liberacoes retornam `PMM_OK` ou um erro para endereco desalinhado, fora do pool,
pagina ja livre ou tentativa de liberar metadados. A liberacao de um bloco e
validada por inteiro antes de alterar bits, sem liberacao parcial em caso de
erro. O cliente deve liberar somente paginas que possui; o PMM nao guarda
identidades de donos nem fronteiras de cada chamada de alocacao.

As operacoes protegem o bitmap e os contadores desabilitando interrupcoes e
restaurando o estado anterior, no contexto atual de uma CPU. Nao ha suporte
a alocacoes concorrentes em SMP ou NMI. Reinicializar um PMM ativo e rejeitado.
O alocador nao recupera automaticamente memoria do bootloader ou ACPI e nao
implementa heap/malloc, swap ou paginacao sob demanda. O VMM usa esse alocador
para obter a propria raiz e as tabelas criadas em sua janela virtual.

`pmm` apresenta paginas totais, livres, alocadas, metadados, endereco e bytes
do bitmap. A identidade `total = livres + alocadas + metadados` e mantida;
as paginas de metadados nao sao contadas como alocacoes do cliente. `memory`
preserva o retrato do boot e tambem mostra os bytes livres atuais do PMM.

```sh
make check-pmm
```

O nucleo independente de hardware e testado com ASan/UBSan para esgotamento,
reutilizacao, unicidade, alinhamento, fragmentacao, blocos contiguos, liberacao
dupla/invalida, metadados e faixas acima de 4 GiB. No QEMU, `selftest=pmm`
verifica a pagina zerada, escreve e le paginas alocadas, testa protecoes e
restaura os contadores depois de liberar tudo. Com 5 GiB, tambem aloca um
bloco acima de 4 GiB e verifica escrita/leitura nele. Sao usados 128, 512 e
5120 MiB em BIOS e UEFI. Logs: `build/boot-pmm-{MiB}-{bios|uefi}.log`.

## Memoria virtual do kernel

O VMM usa paging x86_64 de quatro niveis, com paginas de 4 KiB para novos
mapeamentos. Aloca uma PML4 propria pelo PMM, copia a raiz ativa do Limine e
carrega seu endereco fisico em CR3. As tabelas filhas herdadas continuam
compartilhadas: kernel, pilhas, HHDM, framebuffer, ACPI e LAPIC permanecem
acessiveis. A memoria dessas tabelas do bootloader ainda nao pode ser recuperada.

Uma entrada livre entre os slots PML4 384 e 507 define uma janela privada de
512 GiB. Seu endereco e descoberto em runtime, sem sobrescrever os slots do
kernel, do HHDM ou do LAPIC. Somente essa janela pode ser alterada pela API.
Mapeamentos herdados podem ser consultados, mas map/protect/unmap fora da
janela retornam `VMM_PROTECTED`.

```c
#include <vmm.h>
#include <pmm.h>

struct vmm_stats stats;
vmm_get_stats(&stats);
uint64_t va = stats.window_base + 0x1000;
uint64_t physical;
if (vmm_alloc_page(va, VMM_WRITE, &physical) == VMM_OK) {
    // va e um endereco virtual gravavel de uma pagina inicialmente zerada.
    // Desconecte todos os aliases antes de liberar a pagina fisica.
    if (vmm_unmap_page(va, 0) == VMM_OK) pmm_free_page(physical);
}
```

`vmm_map_page(va, physical, flags)` associa uma pagina fisica ja alocada pelo
PMM, permitindo aliases. `vmm_alloc_page` e uma conveniencia que aloca e zera
uma pagina, cria o mapeamento e devolve seu endereco fisico ao chamador.
Se a operacao falhar, essa pagina e devolvida ao PMM. O endereco virtual deve
ser canonico, alinhado a 4096 e pertencer a janela; a pagina fisica deve estar
alinhada, dentro da largura fisica da CPU e alocada no PMM (fora do bitmap).

`vmm_unmap_page` remove a traducao e opcionalmente devolve o endereco fisico.
**Nao libera a pagina de dados**: o chamador continua dono dela e deve remover
todos os aliases antes de usar `pmm_free_page`. Nao ha contagem de referencias
nem isolamento entre processos nesta etapa. Tabelas intermediarias vazias que
foram criadas pelo VMM sao liberadas automaticamente; a PML4 propria permanece.

`vmm_protect_page` troca as permissoes sem remapear o frame. Todos os novos
mapeamentos sao supervisor: `0` permite leitura, `VMM_WRITE` permite escrita,
e `VMM_EXEC` permite leitura/execucao. Dados recebem NX; W+X e rejeitado para
o mesmo mapeamento. CR0.WP e IA32_EFER.NXE sao habilitados. Essa politica e
local aos novos mapeamentos: os aliases herdados do HHDM mantem suas permissoes.

`vmm_translate` informa endereco fisico, tamanho de pagina e permissoes efetivas,
considerando todos os niveis. Le mapeamentos herdados de 2 MiB e 1 GiB sem
dividi-los. Novos mapeamentos duplicados sao rejeitados. Uma falha de alocacao
desfaz as tabelas parciais e preserva os mapeamentos anteriores.

Alteracoes invalidam a TLB com `invlpg`; a remocao de tabelas intermediarias
tambem recarrega CR3 antes de devolver as paginas ao PMM. As operacoes sao
serializadas com interrupcoes desabilitadas em uma CPU. Nao ha PCID, LA57,
SMP/TLB shootdown ou swap. O gerenciador de espacos por processo adiciona
mapas de usuario privados e troca CR3 ao escalonar. O caminho exige
suporte a NX e preserva as tabelas existentes sem redesenhar o HHDM.

```sh
make run
# No terminal: vmm
make check-vmm
```

O teste do nucleo usa ASan/UBSan e um provedor de paginas limitado para verificar
rollback por OOM em varios niveis, aliases, protecao, heranca, huge pages e
ausencia de vazamentos. No QEMU, sao testados CR3, acesso a mapeamentos distintos,
aliases, traducao, execucao RX, remocao e limpeza em BIOS/UEFI, com 128, 512 e
5120 MiB. O caso de 5 GiB tambem mapeia um frame fisico acima de 4 GiB.
Tres cenarios adicionais provocam page faults reais: leitura apos unmap,
escrita em pagina somente leitura e execucao em pagina NX. Os testes conferem
vetor 14, erro e CR2. O comando `vmm` mostra apenas as tabelas proprias; o PMM
registra essas paginas como alocacoes normais do seu cliente VMM.

Referencia: [Intel SDM, System Programming Guide](https://www.intel.com/content/dam/www/public/us/en/documents/manuals/64-ia-32-architectures-software-developer-system-programming-manual-325384.pdf).

## Heap do kernel

`kernel/include/heap.h` fornece `kmalloc`, `kcalloc`, `krealloc` e `kfree`.
Os ponteiros tem alinhamento de 16 bytes. `kmalloc(0)`, produtos com overflow
em `kcalloc` e pedidos que nao cabem retornam NULL. `krealloc(NULL, n)` aloca;
`krealloc(p, 0)` libera; se o crescimento falhar, o bloco original continua
valido e preserva os dados. `kfree(NULL)` e aceito. Ponteiros invalidos e
liberacoes duplicadas detectadas causam panic de memoria (codigo 10).
Redzones e veneno de blocos livres ajudam a detectar escritas invalidas;
os limites dessas verificacoes estao descritos abaixo.

O heap usa first fit, divisao de blocos e fusao de vizinhos livres. Cada arena
tem pelo menos 16 KiB, cresce em multiplos de 4 KiB e pode conter varios blocos.
Uma arena completamente livre e desmapeada e suas paginas de dados retornam ao
PMM; as tabelas vazias retornam pelo VMM. Falhas durante o mapeamento de uma
arena desfazem todos os mapeamentos parciais. Encolher um bloco via `krealloc`
mantem sua capacidade ate a liberacao.

O intervalo `[window_base + 1 GiB, window_base + 1 GiB + 64 MiB)` fica reservado
ao heap dentro da janela do VMM. Outros clientes do VMM devem evitar esse
intervalo. O limite de 64 MiB inclui metadados, guardas e fragmentacao; a memoria fisica
e adquirida somente sob demanda. Todas as paginas do heap sao supervisor,
gravaveis e NX. `heap_init` nao aloca paginas e nao altera o baseline do PMM.

Esta implementacao e para uma CPU, com interrupcoes desabilitadas durante as
operacoes; nao deve ser chamada de IRQ/NMI e ainda nao oferece locks para SMP,
slabs, alinhamentos especiais ou tempos de resposta limitados. O heap nao e
usado pelos caminhos de log/panic, que continuam independentes de alocacao.

```sh
make run
# No terminal: heap (contadores), pmm e vmm
make check-heap
```

Os testes ASan/UBSan verificam overflow, OOM, alinhamento, preservacao de dados,
realloc, fusao, ponteiros invalidos e milhares de operacoes intercaladas.
O QEMU testa BIOS/UEFI com 128, 512 e 5120 MiB, incluindo falha injetada apos
mapear duas paginas, paginas RW/NX e retorno dos contadores PMM/VMM ao estado
anterior. O comando `heap` mostra alocacoes, arenas, bytes pedidos e mapeados.
O codigo de panic 9 identifica falha de inicializacao do heap.

## Deteccao de acessos invalidos

O handler de page fault (vetor 14) registra CR2, RIP, RSP e o erro bruto da CPU,
alem de decodificar leitura/escrita/execucao, supervisor/usuario, pagina ausente
ou violacao de protecao e os bits RSVD/PK/SS/SGX. Identifica a pagina nula e,
apos iniciar o heap, paginas de dados, guardas e intervalos atualmente livres
do heap. A falha e fatal; nao ha recuperacao automatica ou alocacao no handler.
O diagnostico consulta somente o registro estatico das paginas do heap, sem
percorrer seus metadados possivelmente corrompidos.

Cada arena do heap recebe uma pagina virtual ausente antes e outra depois da
area de dados. Elas nao consomem frames fisicos e provocam page fault imediato
quando atingidas. Uma arena completamente liberada fica desmapeada: acessar
seu antigo endereco tambem provoca page fault, enquanto nao houver reutilizacao.

Cada bloco tem uma redzone de 16 bytes antes do ponteiro e pelo menos 16 bytes
apos o tamanho solicitado; o padding tambem e verificado. Blocos livres ainda
mapeados sao preenchidos com 0xDD. Antes de alocar, liberar ou realocar um
bloco, o heap verifica redzones, veneno, estrutura dos blocos e contadores.
`heap_check()` permite solicitar essa verificacao; o comando `heap` a executa
antes de mostrar as estatisticas. Corromper uma redzone, o veneno ou metadados
verificados gera panic 10 com o endereco detectado. Um ponteiro invalido em
free/realloc e rejeitado antes de acessar dados apontados por ele.

Estas verificacoes nao instrumentam cada acesso de C. Uma escrita dentro de
uma pagina mapeada pode ser detectada apenas na proxima verificacao; uma
leitura de redzone ou de um bloco livre ainda mapeado nao e detectada. Guardas
sao por arena, nao por objeto. Escritas que pulam redzones, que restauram o
padrao esperado ou acessam um endereco ja reutilizado podem passar despercebidas.
Nao ha KASAN, shadow memory, quarentena ou deteccao completa de uso apos liberar.
O HHDM preservado continua oferecendo aliases fisicos; estas protecoes cobrem
os enderecos virtuais do heap. A auditoria acrescenta custo linear no heap.

```sh
make check-access
```

O teste provoca dez falhas separadas em BIOS e UEFI: leitura nula, escrita nas
guardas inferior e superior, leitura apos desmapear uma arena, overflow,
underflow, escrita em bloco livre ainda mapeado, double free, free de ponteiro
interior e execucao no heap NX. Confere panic, CR2, tipo de acesso e regiao,
mantendo a VM interrompida para diagnostico. Os testes do heap com ASan/UBSan
tambem corrompem e restauram redzones e veneno para validar os detectores.

## Diagnostico e teste de memoria pelo terminal

`meminfo` e um alias de `memory`: informa RAM total identificada, bytes
utilizaveis no boot, categorias reservadas e bytes atualmente livres no PMM.
O total e a uniao das regioes USABLE, BOOTLOADER_RECLAIMABLE,
ACPI_RECLAIMABLE, ACPI_NVS, EXECUTABLE_AND_MODULES e BAD_MEMORY. Regioes
sobrepostas contam uma vez. Reservada/MMIO, framebuffer e tipos desconhecidos
ficam fora do total, pois nao permitem deduzir RAM instalada com seguranca.
Memoria defeituosa participa do total identificado, nunca da disponivel.
`memmap`, `pmm`, `vmm` e `heap` detalham cada camada.

`memtest` executa um teste limitado pelo terminal, sem reiniciar: aloca ate 64
paginas proprias, verifica zeros e padroes em todos os seus bytes, testa
kmalloc/kcalloc/krealloc/kfree e confere os contadores PMM/VMM/heap antes e
depois. Usa o intervalo `[window_base + 16 MiB, window_base + 16 MiB + 256 KiB)`,
reservado ao teste. Se esse intervalo estiver ocupado ou faltar memoria,
retorna falha e limpa seus recursos. Nao sobrescreve memoria de outros clientes.
Nao testa toda a RAM fisica e nao substitui um teste de hardware de memoria.
Os selftests fatais de protecao e o esgotamento do PMM ficam na suite externa.

```sh
make check-memory check-pmm check-vmm check-heap check-access check-memtest
```

`check-memtest` chama o comando duas vezes em cada boot, confere a rejeicao de
argumentos, a integridade do heap e a ausencia de alocacoes residuais, em
BIOS/UEFI com 128, 512 e 5120 MiB.

## Gerenciador de processos do kernel

`kernel/include/process.h` oferece criacao, yield, sono em ticks do PIT,
encerramento, espera por filho e consulta de processos. O PID 1 representa o
kernel/terminal na pilha inicial. Existem 32 PCBs e 64 TCBs no total; novos PIDs sao
monotonicos e nao sao reutilizados quando um slot e liberado. PCB agrupa as
threads de um processo; TCB guarda TID, estado, contexto e pilha de uma thread.
`thread_create()` cria outra thread no PID atual, `thread_join()` espera por
seu TID e `thread_exit()` termina somente a thread atual. PIDs e TIDs possuem
contadores independentes. O escalonador escolhe TCBs, enquanto `ps` agrega
estado, ticks e quantidade de threads por PCB. `threads` lista os TCBs.

Cada thread criada recebe uma pilha RW/NX de 16 KiB, com uma pagina ausente
antes e outra depois. As pilhas ocupam o intervalo reservado
`[window_base + 2 GiB, window_base + 2 GiB + 64 * 24 KiB)`, separado do heap e
do memtest. Falhas parciais de alocacao desfazem os mapas e devolvem os frames.
A troca em Assembly preserva os registradores callee-saved e o RSP conforme
a ABI SysV; o contexto de cada processo restaura seu estado de interrupcoes.
O codigo C do kernel usa somente registradores gerais: nao ha contexto FPU/SIMD.

O escalonamento e preemptivo em Round Robin, com fila FIFO explicita de threads
READY. Cada thread recebe quantum de 5 ticks do PIT de 100 Hz (50 ms nominais).
Novas threads e threads acordadas entram no fim da fila, sem duplicatas;
bloqueio e encerramento removem a thread da fila. `process_yield()` permite
ceder antes de esgotar o quantum. O PIT contabiliza ticks, acorda threads e
troca de contexto depois do EOI; a IRQ nao aloca memoria nem libera pilhas.
O processo Idle tem PID 0, TID 0, PCB/TCB e
pilha protegida proprios. So e escolhido quando nenhuma thread comum esta
READY. Executa STI/HLT e volta ao escalonador apos uma interrupcao; nao pode
ser encerrado, coletado ou bloqueado. Kernel e Idle ocupam dois dos 32 PCBs
e dois dos 64 TCBs. A pilha do Idle e suas tabelas sao recursos permanentes.
O log `Base` registra esse consumo para distinguir recursos permanentes de
vazamentos nos testes.

Sem entrada, o terminal fica BLOCKED por teclado, permitindo que o Idle rode.
A IRQ1 acorda os leitores sem trocar de contexto; a verificacao da fila e o
bloqueio ocorrem com IRQs desabilitadas para evitar perda de wakeup. O PIT
acorda threads dormindo e o Idle cede a CPU assim que houver trabalho pronto.
Tarefas com IRQs habilitadas sao preemptadas mesmo sem chamar yield. As regioes
com IRQs desabilitadas adiam a entrega do PIT, portanto o quantum nao e uma
garantia de tempo real.

Estados: READY, RUNNING, BLOCKED e TERMINATED. O campo `blocked_on` distingue
sono pelo PIT, espera por filho, join, teclado e suspensao explicita; todos sao BLOCKED e so voltam a READY
quando sua condicao e satisfeita. Ao retornar da funcao de
entrada, a thread termina com status 0. O processo continua vivo enquanto houver
outra thread ativa. `process_exit(status)` e `kill PID` encerram o grupo inteiro.
`thread_join()` recusa espera por si mesmo, outro PID e ciclos entre threads.
A pilha e liberada somente depois de sair de seu contexto. O registro
TERMINATED e o status permanecem ate o pai chamar `process_wait(pid, &status)`.
Filhos orfaos sao adotados pelo PID 1. Sem wait, zombies ocupam slots, mesmo
quando suas pilhas ja foram devolvidas. O PID 1 e protegido contra kill.

```sh
make run
# No terminal:
ps
spawn
# Use o PID mostrado (demo dorme por aproximadamente 5 segundos):
wait 2
# Ou crie outro e use kill PID, depois wait PID.
make check-processes
```

`ps` mostra PID, pai, estado, quantidade de threads, ticks atribuidos enquanto RUNNING, status e nome.
O tempo ocioso fica atribuido ao Idle; ticks amostrados pelo PIT nao sao uma
medicao precisa de utilizacao de CPU. `spawn` inicia uma demo silenciosa; `kill PID`
encerra um processo fora da CPU com status -9; `wait PID` espera/coleta um filho.
Argumentos invalidos, PID inexistente, self-kill e espera por nao-filho sao
rejeitados. Os ponteiros de funcao/argumento devem permanecer validos.

Alocacoes kmalloc/kcalloc/krealloc feitas por PIDs comuns passam a pertencer
automaticamente ao PCB. As threads compartilham esses recursos; terminar uma
thread nao libera o heap do processo enquanto outra estiver viva. Ao terminar
o grupo inteiro, a limpeza ocorre fora de suas pilhas, antes da coleta por wait.
`kfree` remove o registro; `krealloc` preserva a posse em sucesso e falha.
O PID 1 mantem as alocacoes gerais do kernel sem coleta automatica.

`kernel/include/process_resources.h` oferece `process_alloc_pages()` para
blocos fisicos e `process_map_pages()` para paginas virtuais com posse pelo PID.
Os mapas usam uma janela reservada de 32 MiB a partir de `window_base + 3 GiB`.
`process_resource_attach()` registra outros recursos com um destrutor;
`process_resource_release()` fecha um recurso antecipadamente e
`process_resource_transfer()` transfere sua posse para um PID vivo. Cada recurso
so pode ser registrado uma vez. O registro global comporta 256 entradas;
esgotamento retorna falha sem deixar alocacoes sem dono.

Destrutores executam uma vez, com IRQs desabilitadas; devem ser curtos, nao
bloquear e nao alocar. Pilhas, tabelas vazias, heap associado, blocos e mapas
gerenciados sao devolvidos automaticamente em exit/kill. Usar PMM/VMM brutos
continua exigindo gerenciamento explicito: esses acessos tambem sustentam as
tabelas e o heap do kernel e nao sao atribuidos automaticamente ao chamador.
Objetos compartilhados entre PIDs precisam de transferencia ou de um dono
com vida util suficiente; nao ha garbage collector ou contagem geral de referencias.

`process_create` executa callbacks confiaveis em ring 0, na mesma CR3 e memoria
do kernel, em uma CPU. Uma excecao em um processo do kernel para o sistema
inteiro; o panic registra seu PID. Para executar codigo em Ring 3 com memoria
privada, use `process_create_user`, descrita abaixo. As APIs publicas de tarefas
nao devem ser chamadas em IRQ/NMI ou com interrupcoes desabilitadas.

Os testes ASan/UBSan verificam estados, round-robin, sono com wrap do contador,
wait, adocao de orfaos, esgotamento dos slots e milhares de ciclos de PIDs.
No QEMU BIOS/UEFI com 128, 512 e 5120 MiB, os testes verificam trocas reais de
pilha, registradores, alternancia, sono, exit/wait, kill, retorno ao terminal,
rollback em OOM e contadores de memoria restaurados. Uma VM separada provoca
page fault na guarda da pilha e confere CR2 e PID. Falha de inicializacao do
gerenciador usa panic 11.

## Escalonador preemptivo

`sched` mostra quantum, tamanho da fila de prontas, trocas, preempcoes e ticks
adiados em regioes criticas. `thread_suspend(tid)` suspende uma thread READY ou
a propria thread RUNNING; `thread_resume(tid)` a recoloca no fim da FIFO.
Threads ja bloqueadas por outro motivo, encerradas e threads de sistema sao
recusadas. Sono temporizado usa `process_sleep(ticks)`, com ticks ate INT64_MAX.

A entrada de IRQ salva os 15 GPRs e o frame RIP/CS/RFLAGS/RSP/SS. A troca SysV
em Assembly suspende o caminho de retorno na pilha de cada thread. Ao retomar
uma thread interrompida, o handler termina e IRETQ restaura o estado completo
de registradores gerais e flags. O EOI precede a troca para que a proxima
thread possa receber IRQs. Nao ha salvamento de FPU/SIMD; os callbacks do kernel
sao compilados com `-mgeneral-regs-only`.

Fila, PCBs/TCBs, PMM/VMM, heap e registro de recursos sao acessados em regioes
com IRQs desabilitadas. Logs, serial, framebuffer e redesenho do terminal
tambem serializam suas estruturas. `process_preempt_disable()` e
`process_preempt_enable()` permitem regioes criticas aninhadas entre threads,
mantendo IRQs habilitadas. O PIT continua contando e marca uma troca pendente;
a ultima chamada enable atende essa troca se houver trabalho pronto. Essas
regioes nao substituem CLI ao compartilhar dados com IRQs, nao podem bloquear
ou terminar a thread e devem ser curtas. O controle de energia impede troca
de tarefa durante suas sequencias de hardware. Esta protecao e para uma CPU,
sem locks SMP ou garantias contra callbacks ring 0 que desabilitem IRQs para sempre.

```sh
make check-scheduler
# No terminal: sched, ps, threads
```

Os testes ASan/UBSan verificam FIFO, quantum, wakeup, suspensao, remocao de
tarefas encerradas, capacidade e 5000 rotacoes da fila. No QEMU BIOS/UEFI com
128, 512 e 5120 MiB, duas tarefas em Assembly mantem todos os GPRs ocupados e
DF/flags conhecidos, sem yield. Uma terceira tarefa continua rodando, suspende
e retoma uma delas e confirma adiamento aninhado de preempcao. Workers de
memoria alocam e alteram dados durante trocas pelo PIT e verificam que
PMM/VMM/heap voltam ao baseline ao terminar. Logs concorrentes sao validados
pela sequencia serial e o console continua legivel.

## Kernel Mode e User Mode

A GDT possui codigo/dados DPL3 (CS=0x2b, SS=0x33). Cada thread possui pilha de
kernel RW/NX de 16 KiB com guardas; o escalonador atualiza TSS.RSP0 e CR3 antes
de trocar o contexto. IRETQ inicia o programa em CPL3 com IF=1 e IOPL=0.
Interrupcoes, excecoes e INT80 retornam ao kernel pela pilha indicada no TSS.
Os registradores gerais e bases FS/GS iniciais sao zerados, exceto o argumento
escalar explicito do programa.

Cada processo de usuario recebe PML4/PDPT/PD/PT privados na metade baixa,
sem mapas de identidade herdados. Paginas novas sao zeradas; codigo e RX,
dados/pilha RW-NX, sem W+X. A metade alta compartilha os mapas supervisor do
kernel, HHDM, heap e pilhas de kernel. Nao existe API de usuario para mapear
frames fisicos arbitrarios. Destruicao libera paginas e tabelas privadas
somente depois de sair da CR3 e pilha do processo encerrado.

`process_create_user(nome, imagem, tamanho, offset_entrada, argumento)` carrega
uma imagem plana fornecida pelo kernel, de ate 64 KiB. Enderecos: codigo
0x400000, dados 0x600000 e pilha 0x800000..0x804000. O limite do espaco privado
e 64 paginas de dados (256 KiB, sem contar tabelas). A pilha inicial fica em
0x803ff8, sem endereco de retorno: o programa deve chamar exit.

A ABI v1 usa `INT 0x80`, numero em RAX, argumentos RDI/RSI/RDX e retorno
em RAX: exit=0, yield=1, getpid=2, gettid=3, ticks=4, write=5, read=6, sleep=7.
Numero desconhecido retorna -38. Buffers de I/O passam por validacao integral
e copia segura, com permissoes de usuario e escrita conforme a direcao. SYSCALL/SYSENTER
nao configurados sao desabilitados; IOPL/TSS negam acesso a portas. FPU/SIMD
provocam #NM e encerramento, pois seu contexto ainda nao e salvo.

Excecoes originadas em CPL3 encerram o processo com status `-(256 + vetor)`;
por exemplo, page fault retorna -270. O log registra PID, vetor, erro, RIP e
CR2. Falhas em CPL0 continuam fatais. NMI, double fault e machine check tambem
permanecem fatais. O pai pode coletar o status com wait; o kernel continua.

```sh
make check-user
# No terminal: spawnuser, ps, wait PID, meminfo
```

`spawnuser` executa uma demonstracao real em CPL3 que termina com status 42.
`ps` indica R0/R3. Os testes BIOS/UEFI em 128, 512 e 5120 MiB verificam dois
processos preemptivos com dados distintos no mesmo endereco virtual, chamadas
INT80, limpeza apos exit e rollback quando falta pilha de kernel. Outros 26
testes provocam acessos ao kernel/HHDM, violacoes RX/NX, guardas, instrucoes
privilegiadas, excecoes, tentativa de IRET para Ring 0 e pilha de usuario
invalida (excecao, INT80 e retorno de IRQ). O retorno a Ring 3 valida seletores,
RIP, RSP e flags antes do IRETQ. Uma vitima continua executando quando outro PID tenta ler sua pagina
privada; o terminal permanece funcional e os contadores de memoria retornam
ao baseline. O nucleo de enderecamento tambem e testado com ASan/UBSan,
incluindo OOM, limites e tentativa de destruir a CR3 ativa.

O loader ELF64 descrito abaixo carrega executaveis estaticos dos modulos Limine.
Ainda nao ha sistema de arquivos, fork/exec, sinais, syscalls para criar threads
de usuario, TLS, FPU/SIMD ou SMP. Cada processo
Ring 3 inicia uma thread; as multiplas threads existentes continuam disponiveis
para processos do kernel. As protecoes descritas cobrem acessos arquiteturais
pela CPU, sem promessa de protecao contra canais laterais.

## Chamadas de sistema

As seis chamadas solicitadas, `write`, `read`, `exit`, `getpid`, `sleep` e
`yield`, possuem wrappers C freestanding em `user/include/syscall.h`. A
[especificacao da ABI](docs/syscalls.md) descreve registros, numeros, retornos
e erros. stdout/stderr sao fd 1/2 no console e serial; stdin e fd 0 no PS/2.
I/O aceita ate 4096 bytes por chamada e preserva bytes NUL na serial, sem
exigir terminador. read nao faz eco/edicao e pode retornar menos bytes.
sleep recebe milissegundos, bloqueia pelo PIT e deixa outras tarefas executar.

O dispatcher habilita IRQs apos validar a entrada de CPL3 e volta a mascara-las
antes do retorno validado por IRETQ. Assim, read e sleep podem bloquear sem
parar o temporizador. Nao sao usados ponteiros de usuario diretamente em C:
preflight verifica todas as paginas e buffers de kernel intermedeiam a copia.
O caminho de copyout exige USER e WRITE, sem conceder a permissao especial do
loader para popular paginas RX. Erros retornam -errno em vez de causar panic.

```sh
make check-syscalls
# No terminal do NexusOS: sysdemo
# Digite uma linha e Enter; o programa Ring 3 mostra a resposta e termina.
```

Programas de teste C/Assembly verificam a ABI e as chamadas em CPL3. IRQs podem
preemptar o kernel dentro de uma syscall. A entrada PS/2 e injetada
pelo QMP apos confirmar o bloqueio; uma tarefa testemunha observa sleep/INPUT.
Sao testados limites, buffers invalidos, paginas somente de leitura, copias que
cruzam paginas, leituras curtas, encerramento durante read e devolucao integral
dos recursos. A demonstracao sysdemo e testada em primeiro plano pelo terminal.

## Execucao de programas ELF64

A ISO inclui alpha.elf e beta.elf como modulos Limine, com codigo C distinto.
O kernel valida os arquivos completos e cria paginas privadas para cada
PT_LOAD: texto RX, constantes RO/NX e dados/BSS RW/NX. BSS e padding sao zerados.
Cada processo recebe CR3 e stack proprios; o entry point do ELF e executado
em Ring 3 com argc/argv/auxv na stack, usando a ABI de syscalls existente.

```sh
make check-elf
# No terminal: programs, elfdemo
# Ou: run alpha, run beta, ps, wait PID
```

elfdemo inicia ambos antes de liberar a execucao e coleta os status, esperados
como zero. Os programas usam os mesmos enderecos virtuais de dados/BSS com
conteudos distintos e verificam que seus dados continuam corretos apos sleep.
Nomes inexistentes e argumentos invalidos retornam ao prompt.

Suporte inicial: ELF64 little-endian System V/AMD64, ET_EXEC estatico, ate
1 MiB de arquivo e 256 KiB de paginas por processo incluindo a stack. Nao ha
PIE, linker dinamico, TLS ou bibliotecas compartilhadas. Paginas compartilhadas
entre segmentos, W+X, invasao da stack/guardas e entry invalido sao recusados.
Veja [formato, limites e testes](docs/elf64.md). Os programas ELF sao arquivos
na ISO, separados do kernel; imagens planas anteriores continuam disponiveis.

## Testes e diagnostico

ps, kill PID e run NOME permitem inspecionar, encerrar e executar processos.
top mostra uma amostra de CPU durante um segundo, ordenada por uso e incluindo
o Idle. A tabela usa ticks do PIT e preserva a identidade por PID.

Os modulos adicionais busy e fault permitem exercitar preempcao e recuperacao.
Execute run busy duas vezes, consulte ps/top e encerre os PIDs com kill e wait.
run fault provoca um page fault de usuario; wait coleta -270 e o terminal
continua funcionando. elfdemo deve executar normalmente depois dessa falha.

```sh
make check-diagnostics
make check-release  # Validacao conjunta para publicar uma versao
```

Os testes repetem fault/recuperacao/kill, comparam PMM/VMM/heap e os numeros de
PCB/TCB e exercitam os comandos pelo QMP em BIOS/UEFI com tres tamanhos de RAM.
Veja [passo a passo e interpretacao dos resultados](docs/diagnostics.md).

Veja tambem a [auditoria dos fundamentos e configuracao do GDB](docs/fundamentals.md).

Referencias oficiais:

- https://cdrdv2-public.intel.com/843827/253665-sdm-vol-1-dec-24.pdf
- https://github.com/Limine-Bootloader/Limine/releases/tag/v12.11.1
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/USAGE.md
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/CONFIG.md
