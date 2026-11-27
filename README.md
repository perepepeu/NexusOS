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
e confirma que a VM permanece ativa ate ser encerrada pelo teste (20s por modo).
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

Tela: framebuffer pronto.
NexusOS: iniciando kernel x86_64...
Bootloader: Limine 12.11.1
Protocolo Limine: OK
NexusOS: kernel inicial pronto. CPU em espera.
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

Referencias oficiais:
- https://github.com/Limine-Bootloader/Limine/releases/tag/v12.11.1
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/USAGE.md
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/CONFIG.md
