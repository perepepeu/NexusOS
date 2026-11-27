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
A arquitetura definitiva do sistema ainda pode ser escolhida.

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
sudo apt-get install build-essential nasm xorriso curl python3 qemu-system-x86 ovmf
make setup-limine
make check-bootloader
```

Limine 12.11.1 e baixado da release oficial, com versao e SHA-256 fixos no
script `scripts/setup-limine.sh`. Seus binarios e licencas ficam em
`vendor/limine`, fora do Git. A ferramenta de instalacao BIOS e compilada no WSL.

`make bootloader-iso` gera `build/nexusos-bootloader.iso`, com boot BIOS e UEFI
x86_64. A imagem desta etapa contem apenas o bootloader, sem kernel.
O menu fica aberto; selecionar NexusOS ainda resulta em erro de kernel ausente.
O caminho reservado para o kernel e `/boot/kernel.elf`, em `boot/limine.conf`.

Para abrir o menu no terminal:

```sh
make run-bootloader       # BIOS
make run-bootloader-uefi  # UEFI com OVMF
```

Use Ctrl+A, depois X, para sair do QEMU. A serial funciona sem interface grafica
ou WSLg. Os comandos usam emulacao TCG, sem depender de permissoes KVM.
O teste `make check-bootloader` inicia as duas variantes sem interface grafica
e verifica a presenca do menu na serial. Logs ficam em `build/boot-bios.log`
e `build/boot-uefi.log`. Isso valida o bootloader, ainda sem executar um kernel.

Referencias oficiais:
- https://github.com/Limine-Bootloader/Limine/releases/tag/v12.11.1
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/USAGE.md
- https://github.com/Limine-Bootloader/Limine/blob/v12.11.1/CONFIG.md
