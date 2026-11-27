# NexusOS

Versao atual: **0.1**. A versao fica registrada em `VERSION` e na tag Git
`v0.1`. Veja as entregas em [CHANGELOG.md](CHANGELOG.md).

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
sudo apt-get install build-essential nasm xorriso curl python3 qemu-system-x86 qemu-system-gui ovmf
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

O formatador sem libc suporta `%s`, `%u` e `%x` para `unsigned int`, `%p` para
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
Ainda nao ha segmentos ou transicoes para processos em ring 3.

A IDT contem 256 interrupt gates, acessiveis apenas pelo kernel. Os stubs
Assembly normalizam o codigo de erro, preservam os 15 registradores gerais,
limpam DF para chamar C e alinham a pilha ao ABI. O retorno usa `iretq` para
restaurar o contexto e RFLAGS. `INT3` registra o breakpoint e retorna; as demais
excecoes param o kernel com nome, vetor, codigo de erro, RIP, RSP e RFLAGS.
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

Referencias oficiais:
- https://github.com/Limine-Bootloader/Limine/releases/tag/v12.11.1
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/USAGE.md
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/CONFIG.md
