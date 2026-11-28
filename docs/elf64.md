# Executaveis ELF64

O loader aceita ELF64 little-endian, System V ABI 0, AMD64, ET_EXEC estatico.
ET_DYN/PIE, PT_INTERP, PT_DYNAMIC, PT_TLS, relocacoes dinamicas e contagens
estendidas nao sao suportados. Sections nao dirigem o carregamento; quando
declaradas, seus intervalos e tamanhos basicos tambem sao validados.

Limites: arquivo de ate 1 MiB, ate 32 program headers, 16 segmentos PT_LOAD e
60 paginas de segmentos, alem de 4 paginas de stack. PT_LOAD deve declarar
PF_R; PF_W cria RW/NX, PF_X cria RX, e apenas PF_R cria RO/NX. W+X e rejeitado.
Segmentos devem estar ordenados por endereco, sem compartilhar paginas.
Alinhamento deve ser 0, 1 ou potencia de dois, com offset/endereco congruentes;
a congruencia de pagina de 4 KiB tambem e exigida. Segmentos de tamanho zero
sao ignorados. p_paddr nunca e usado para escolher paginas fisicas.

Intervalos precisam ficar na metade baixa canonica, acima da pagina zero e
fora da stack/guardas 0x7ff000..0x805000. O entry point deve estar em bytes
de arquivo de um segmento executavel. Overflow, arquivos truncados, segmentos
sobrepostos e limites excessivos sao recusados antes de alocar memoria.

Cada pagina recebe um frame privado zerado; os bytes p_filesz sao copiados e
a diferenca ate p_memsz (BSS), prefixos e padding permanecem zerados. O loader
usa a copia privilegiada do kernel para popular paginas RX/RO, sem tornar o
codigo gravavel pelo usuario. Falhas de alocacao desfazem todo o espaco.

A stack RW/NX ocupa 0x800000..0x804000, com guardas ausentes em ambas as pontas.
RSP inicial e multiplo de 16. Contem argc=1, argv[0]=nome, argv[1]=NULL,
envp[0]=NULL e auxv com AT_PAGESZ=4096, AT_ENTRY=entry e AT_NULL.
O programa deve usar um _start em Assembly que chama C respeitando o ABI,
e terminar por sys_exit. Ainda nao ha argumentos adicionais, ambiente ou libc.

Os programas sao arquivos ELF completos na ISO: /boot/programs/alpha.elf e
/boot/programs/beta.elf. A configuracao module_path/module_string do Limine
fornece um catalogo somente de leitura ao kernel. As paginas dos modulos
permanecem reservadas no mapa fisico; codigo/dados de cada processo sao copias
privadas. Nao e necessario initramfs nesta etapa.

O loader tambem recebe arquivos pelo VFS: `run /mnt/PROGRAMA.ELF` pode carregar
um ELF do FAT32 ou RAMFS. A leitura usa uma copia temporaria de ate 1 MiB,
fecha o descritor e libera a copia apos criar o processo ou ao falhar. O mesmo
validador ELF e isolamento de paginas sao aplicados; veja os testes de
[arquivos e processos](fs-integration.md). O processo inicia com cwd `/`.

Os dois executaveis possuem fontes C distintas e tres PT_LOAD: RX para texto,
RO/NX para constantes e RW/NX para dados/BSS. Ambos usam os mesmos enderecos
virtuais para globais, mas valores e frames diferentes. O entry point e
0x401010; ele nao coincide com a base do segmento nem com a base das imagens
planas antigas. O _start confere a pilha e chama C pelo ABI SysV.

No terminal:

```text
programs
elfdemo
run alpha
run beta
run /mnt/PROGRAMA.ELF
ps
wait PID
```

programs lista o catalogo. elfdemo cria alpha/beta antes de permitir que
executem, aguarda ambos e mostra seus status. run NOME cria um processo em
segundo plano; use o PID mostrado em wait para coletar o resultado. Um nome
fora do catalogo e resolvido como caminho absoluto ou relativo pelo VFS.
Caminhos inexistentes, argumentos extras e arquivos invalidos sao recusados.

Testes:

```sh
make check-elf-unit
make check-elf
```

O teste ASan/UBSan cobre cabecalhos/segmentos hostis, arquivos truncados e
desalinhados, permissoes, BSS, stack/argv, isolamento de frames, falhas em cada
posicao de alocacao, rollback e 10000 mutacoes deterministicas.

No QEMU BIOS/UEFI com 128, 512 e 5120 MiB, o teste confirma CR3 e frames
distintos para texto/constantes/dados/stack, BSS inicial e nomes argv corretos.
Os dois programas comecam antes de qualquer um terminar. Arquivos invalidos
nao consomem PID nem paginas; uma falha na alocacao da pilha de kernel desfaz
o espaco ELF inteiro. Nove processos ELF sinteticos provocam page faults reais
ao escrever texto/constantes, executar dados/constantes/stack e acessar kernel,
pagina zero e guardas. Cada fault encerra somente o PID e restaura os recursos.
O teste exercita tambem elfdemo, run e wait no terminal, verificando os status
e os contadores de PMM/VMM/heap ao final.

A ISO inclui tambem os modulos busy (carga de CPU) e fault (page fault previsto)
para os [testes de diagnostico](diagnostics.md). Eles so executam quando
solicitados pelo terminal ou pelo perfil de autoteste.

Referencias: [ELF gABI](https://gabi.xinuos.com/elf/08-pheader.html) e
[configuracao Limine](https://github.com/Limine-Bootloader/Limine/blob/v12.x/CONFIG.md).
