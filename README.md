# NexusOS

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
[5] [INFO] [boot] NexusOS: kernel inicial pronto. CPU em espera.
```

Ao terminar, a CPU aguarda com `cli; hlt`, sem consumir um nucleo em loop ativo.
Ainda nao ha shell, teclado ou interrupcoes configuradas. A saida do kernel
aparece na tela e no terminal serial dos comandos `make run` e `make run-uefi`.
O header oficial do protocolo e versionado em `kernel/include/limine.h`, com
licenca e commit de origem preservados. Falhas no protocolo imprimem PANIC e
interrompem a inicializacao.

## Mensagens na tela

O console usa o framebuffer RGB de 32 bits entregue pelo Limine, respeitando
o pitch e as mascaras de cor. Uma fonte propria 5x7 ampliada 2x desenha letras,
numeros e pontuacao basica sobre fundo escuro. Letras sao exibidas em maiusculas;
caracteres nao suportados usam `?`. O console suporta quebra de linha e ajuste
automatico ao limite horizontal. Ao esgotar as linhas, limpa a tela e reinicia
no topo. Se nao houver framebuffer compativel, a inicializacao continua pela
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
O logger opera no contexto atual de uma CPU com interrupcoes desabilitadas.
Excecoes de hardware (como page fault e divisao por zero) ainda dependem da
implementacao futura da IDT e de seus handlers.

Referencias oficiais:
- https://github.com/Limine-Bootloader/Limine/releases/tag/v12.11.1
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/USAGE.md
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/CONFIG.md
