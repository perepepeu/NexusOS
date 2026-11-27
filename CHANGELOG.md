# Historico de versoes

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
