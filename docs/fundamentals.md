# Auditoria dos fundamentos

Os requisitos de v0.0.1/v0.1 foram conferidos novamente apos v0.3. Foram
corrigidas lacunas no runtime, na protecao explicita do framebuffer, no dump
de registradores e na configuracao do GDB. Essas alteracoes sao posteriores
a tag v0.3; a tag e a Release publicada permanecem imutaveis.

## Reproduzir

```sh
make check-runtime-unit check-console-unit
make check-gdb
make check-basics
make check-interrupts check-keyboard check-terminal check-memory
```

check-basics verifica 640x480/800x600/1024x768 em BIOS/UEFI e injeta nove
situacoes de protocolo: revisao incompatível e ausencia de info/HHDM/endereco
do kernel/mapa de memoria/framebuffer/RSDP/modulos/cmdline. As respostas
obrigatorias param com panic controlado; as opcionais possuem fallback.

Os casos controls medem cinco segundos de repouso: comparam uptime do guest
com monotonic do host (tolerancia de teste de 20%, incluindo latencia serial)
e exigem CPU do processo QEMU abaixo de 35%. Sao medidas do ambiente TCG,
nao calibracao de hardware fisico nem promessa de CPU identica em outro host.
Tambem verificam uma rajada de teclas, historico alem de oito entradas, ticks
avancando apos o teclado, tres reboots da mesma VM e shutdown ACPI real.
Eventos QMP podem incluir resets do firmware; o numero de boots completos do
kernel deve ser exatamente quatro e nao pode haver reset antes dos comandos.

Os testes unitarios do framebuffer usam padding no pitch e guardas na memoria,
repetem rolagem e edicao e exercitam RGB/BGR e metadados invalidos. As funcoes
de memoria/strings seguem contratos C: memcpy exige intervalos distintos;
memmove suporta overlap; strncpy pode nao terminar em NUL se o limite esgotar.
Elas nao substituem a validacao de buffers de usuario das syscalls.

## GDB

No WSL, instale gdb se necessario. No primeiro terminal, dentro do projeto:

```sh
make debug
# Ou: make debug-uefi
# Sem janela grafica: make debug DISPLAY_BACKEND=none
```

No segundo terminal WSL, na mesma pasta:

```sh
make gdb
```

O script conecta em 127.0.0.1:1234 e usa breakpoint de hardware na primeira
instrucao de kmain, antes de seu prologo. RSP deve ter resto 8 modulo 16 na
entrada C; o CALL foi feito com RSP alinhado a 16. Use continue para prosseguir,
info registers para inspecionar e detach para devolver execucao ao guest.
check-gdb verifica tambem DATA inicializada/BSS zero e boot depois de detach.

## Teste continuo

```sh
# Teste curto do mecanismo:
python3 scripts/check-basics.py --soak-seconds 60 --mode bios
# Uma hora REAL, BIOS (nao acelera o relogio):
make check-soak
# Para repetir em UEFI:
python3 scripts/check-basics.py --soak-seconds 3600 --mode uefi
```

Durante soak, heartbeats/ps/memtest/PMM/VMM/heap sao exercitados periodicamente.
O guest nao pode reiniciar, desligar ou entrar em panic. O progresso fica em
build/basics-soak-progress.json; so existe sucesso de uma hora quando o
resultado final indicar continuous_seconds >= 3600. Um teste curto nao
completa esse requisito. Os resultados detalhados ficam localmente em build/.
