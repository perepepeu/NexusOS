# Arquivos, processos e programas no disco

Implementacao posterior a v0.3; nao cria uma nova release. As chamadas e suas
estruturas publicas estao em [syscalls.md](syscalls.md). O armazenamento usa
o [driver ATA PIO](storage.md), [VFS](vfs.md) e [FAT32 gravavel](fat32-write.md).

## Syscalls e processos

- [x] open e close, com descritores privados por PID a partir de 3.
- [x] read e write em arquivos; console/teclado mantidos em 0/1/2.
- [x] seek, stat, mkdir, unlink e readdir.
- [x] Validacao integral de caminhos, buffers, overflow e permissoes USER/WRITE.
- [x] Modos READ/WRITE, mounts somente leitura e atributos FAT protegidos.
- [x] Liberacao de descritores e cwd apos exit, kill ou excecao do processo.
- [x] Carregamento de ELF64 pelo VFS diretamente do disco.

Tambem ha chdir, getcwd e sync. As threads de um PID compartilham a tabela de
arquivos. Um processo novo comeca com cwd `/` e sem FDs de arquivo herdados.
Nao ha UID/GID ou permissoes Unix. As operacoes sao serializadas contra outras
tarefas; interrupcoes continuam habilitadas durante I/O. Append de uma chamada
de write nao se intercala com outro write. O limite por syscall e 4096 bytes.

`run CAMINHO` le um arquivo regular de ate 1 MiB em uma copia temporaria,
valida o ELF, cria paginas privadas e inicia o entry point em Ring 3. Libera
a copia e fecha o arquivo em sucesso ou falha. Usa o mesmo loader e as mesmas
permissoes W^X dos modulos Limine. Nomes do catalogo, como `run alpha`, continuam
funcionando. Nao ha uma syscall exec nesta etapa.

## Comandos do terminal

| Comando | Uso |
| --- | --- |
| ls, cd, pwd | Listar, mudar e consultar o diretorio atual |
| mkdir, touch | Criar diretorio ou arquivo; touch preserva conteudo existente |
| cat | Mostrar conteudo do arquivo |
| write, append | Substituir/acrescentar texto, incluindo quebra de linha |
| rm, rmdir | Excluir arquivo ou diretorio vazio |
| cp ORIGEM DESTINO | Copiar arquivo regular, inclusive entre volumes |
| mv ORIGEM DESTINO | Renomear/mover dentro do mesmo volume |
| df [CAMINHO] | Capacidade e bytes livres dos volumes ou do caminho |
| mount | Listar montagens, tipo, dispositivo e modo RO/RW |
| mount DIRETORIO | Montar o volume RAMFS de demonstracao |
| mountfat DISPOSITIVO DIRETORIO | Montar FAT32 somente leitura |
| mountfatrw DISPOSITIVO DIRETORIO | Montar FAT32 para leitura/escrita |
| umount DIRETORIO | Sincronizar e desmontar se nao houver referencias |
| sync [CAMINHO] | Sincronizar um volume ou todos os volumes |
| run CAMINHO | Executar ELF64 armazenado no VFS |

Exemplo com um disco FAT32 de testes contendo seu PROG.ELF, ligado a ATA:

```text
mountfatrw ata0 /mnt
mount
df /mnt
mkdir /mnt/docs
cd /mnt/docs
touch NOTA.TXT
write NOTA.TXT primeira linha
append NOTA.TXT segunda linha
cat NOTA.TXT
cp NOTA.TXT COPIA.TXT
mv COPIA.TXT FINAL.TXT
ls .
rm FINAL.TXT
cd /
run /mnt/PROG.ELF
wait PID
sync
umount /mnt
```

Substitua PID pelo numero mostrado por run. O programa precisa ser um ELF64
estatico compativel com a ABI; os arquivos build/user/alpha.elf e beta.elf
sao exemplos. A suite prepara seus proprios arquivos e executaveis de teste.
O parser nao aceita espacos em caminhos nem aspas. FAT32 cria nomes ASCII 8.3;
arquivos LFN existentes sao acessados pelos aliases curtos. cp nao e recursivo,
exige destino explicito, recusa copiar sobre o proprio arquivo e pode deixar
destino parcial se faltar espaco ou ocorrer erro de I/O. mv entre volumes e
recusado. df no RAMFS informa capacidade limitada por seus slots e buffers;
no FAT32 RO, a contagem do FSInfo pode ser estimada e aparece identificada.

## Validacao reproduzivel

No Ubuntu/WSL, com as dependencias do projeto, mtools e dosfstools:

```sh
make check-fs-syscalls
make check-fs-integration
make check-storage
make check-vfs
make check-fat32
make check-fat32-write
```

As suites criam imagens descartaveis em diretorios temporarios; nao usam discos
fisicos. Todos os cenarios QEMU rodam em BIOS e UEFI. Os testes unitarios usam
ASan/UBSan. Resultados detalhados ficam em `build/`, fora do versionamento.
`check-release` inclui as duas novas suites de integracao.

| Requisito de estabilidade | Evidencia |
| --- | --- |
| Leitura/escrita de setores | check-storage: LBA, limites, flush, persistencia e falhas de I/O |
| Arquivos maiores que um cluster | check-fs-integration: DATA.BIN de 12288 bytes por processo e copias de 8197 bytes |
| Arquivos fragmentados | check-fat32 e check-fs-integration: leitura/copia e ELF com cadeia deliberadamente fragmentada |
| Diretorios aninhados | check-fat32-write e comandos na integracao; resolucao relativa e ./.. |
| Criacao/exclusao repetida | check-fat32-write, testes unitarios e ciclos de processos na integracao |
| Operacoes simultaneas | Dois ELF em Ring 3 por ciclo, arquivos privados e append no arquivo compartilhado |
| Persistencia apos desligar/reiniciar | Segundo boot na mesma imagem verifica dados e nao modifica a imagem em modo RO |
| Leitura por outro SO | Linux/WSL: mtype compara todos os bytes; fsck.fat verifica FATs e estrutura |
| Disco cheio | check-fat32-write e testes unitarios: ENOSPC, conteudo preservado e liberacao de clusters |
| Volumes invalidos/corrompidos | check-fat32: geometria, cadeias invalidas/circulares e volume em LBA nao zero |
| Interrupcao inesperada | QEMU morto durante gravacao; boot seguinte recusa RW, permite RO; reparo externo e novo boot funcional |
| Vazamentos | Contadores PMM, heap e VFS restaurados apos exit, kill, excecao, erros do loader e desmontagem |
| ELF no disco | run pelo terminal, dois programas simultaneos, W+X/arquivo curto/limite rejeitados |

`check-fs-syscalls` executa dois ELF em Ring 3 sobre RAMFS (duas VMs).
`check-fs-integration` executa cinco cenarios por firmware (dez VMs):

1. FAT32 RW: dois ciclos de dois ELF, isolamento de FDs, ponteiros invalidos,
   modos, seek/stat/readdir, offsets intactos apos erros, esgotamento da tabela,
   append concorrente e arquivos maiores que um cluster. Um processo bloqueado
   e morto com arquivos abertos; outro provoca UD2. O volume ocupado nao pode
   ser desmontado. Os contadores voltam ao baseline em cada ciclo. O terminal
   exercita os comandos e inicia outro ELF pelo caminho.
2. Novo boot com os mesmos dados: verifica arquivos de cada PID, copia e
   persistencia. mtools verifica os bytes e fsck.fat exige volume limpo.
3. Interrupcao forcada: um checkpoint somente de autoteste para a CPU apos
   gravar/flush da FAT e antes de publicar a entrada do diretorio. O host mata
   QEMU sem shutdown, deixando clusters sem referencia e o bit dirty marcado.
4. Novo boot no volume interrompido: escrita recusada, leitura disponivel e
   arquivo preexistente intacto. O teste exige que o volume continue dirty.
5. Apos `fsck.fat -a` externo na imagem descartavel: novo boot RW, execucao de
   ELF, escrita, liberacao de recursos, desmontagem e fsck final limpo.

Isso valida um ponto real de interrupcao e a recuperacao com ferramenta externa.
O kernel nao implementa journal nem reparo automatico; nao ha garantia de
atomicidade diante de qualquer corte em qualquer setor. Outros pontos de erro
sao cobertos por injecao de falhas nos testes unitarios e de escrita FAT32.
