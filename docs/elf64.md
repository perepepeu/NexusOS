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

Teste do nucleo do loader:

```sh
make check-elf-unit
```

O teste ASan/UBSan cobre cabecalhos/segmentos hostis, arquivos truncados e
desalinhados, permissoes, BSS, stack/argv, isolamento de frames, falhas em cada
posicao de alocacao, rollback e 10000 mutacoes deterministicas.

Referencias: [ELF gABI](https://gabi.xinuos.com/elf/08-pheader.html) e
[configuracao Limine](https://github.com/Limine-Bootloader/Limine/blob/v12.x/CONFIG.md).
