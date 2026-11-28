# VFS — Virtual File System

Implementacao posterior a v0.3, sem nova versao publicada.

- [x] Interface generica de sistemas de arquivos (`vfs_volume`, `vfs_ops`).
- [x] Estruturas de arquivos, diretorios e volumes (`vfs_node`, `vfs_file`).
- [x] Resolucao de caminhos absolutos e relativos ao cwd do processo.
- [x] Interpretacao de `.` e `..`, incluindo raizes de volumes montados.
- [x] Montagem de volumes em diretorios existentes.
- [x] Desmontagem segura com referencias, dependencias e sincronizacao.
- [x] Tabela de descritores independente por PID, compartilhada entre threads.
- [x] Posicao de leitura/escrita, seek, EOF, append e truncamento.
- [x] Serializacao das operacoes e integracao ao encerramento dos processos.

O primeiro backend e RAMFS. A raiz `/` e um volume, `/tmp` inicia com outro
volume montado e `/boot/modules` expoe os modulos Limine em um terceiro volume
somente de leitura. Veja [RAMFS e inicializacao](ramfs.md).

As [syscalls de arquivos](syscalls.md) usam as mesmas tabelas por PID.
O terminal tambem oferece touch/cp/df, listagem de montagens e execucao de
ELFs pelo caminho. Veja [integracao e testes de estabilidade](fs-integration.md).
`/etc/motd` contem uma mensagem de demonstracao. Os dados ficam
na RAM e desaparecem ao reiniciar. Montar um volume nao formata discos nem
interpreta automaticamente particoes: o [backend FAT32 de leitura](fat32.md) pode
usar `vfs_volume.device` para acessar a camada de blocos ja implementada.

## Experimentar no terminal do NexusOS

```text
pwd
ls /
cat /etc/motd
mkdir /tmp/docs
cd /tmp/docs
write nota primeira linha
append ./nota segunda linha
cat ../docs/nota
ls .
umount /tmp
cd /
mount /mnt
write /mnt/arquivo exemplo
cd /mnt
umount .
cd ..
umount /mnt
mount /mnt
cat /mnt/arquivo
umount /mnt
vfsinfo
```

Os dois primeiros `umount` acima devem informar `volume ocupado`, porque o
cwd esta dentro do volume. Depois de sair, a desmontagem pode prosseguir.
O volume de demonstracao continua existindo na RAM apos desmontar, portanto
remonta-lo preserva seu conteudo durante a mesma execucao do kernel.

`mount DIRETORIO` usa um unico volume RAMFS de demonstracao, podendo ser montado
em apenas um local por vez. A API aceita outros volumes independentes, como
os usados pelos autotestes. `ls [CAMINHO]`, `pwd`, `cd [CAMINHO]`, `mkdir`,
`cat`, `write`, `append`, `umount` e `vfsinfo` completam os comandos desta etapa.
`write` substitui o conteudo; `append` acrescenta; ambos adicionam uma quebra
de linha ao texto. O parser do terminal aceita um caminho sem espacos e nao
implementa aspas, redirecionamento ou pipes.

## Testar no WSL

Dentro da pasta do projeto:

```sh
make check-vfs-unit
make check-vfs
```

O teste unitario usa AddressSanitizer e UndefinedBehaviorSanitizer. Verifica
caminhos, barras repetidas, componentes longos, arquivo usado como diretorio,
montagens aninhadas, remount, volume somente leitura, falha de sync, tabelas
cheias, EOF, gaps zerados, truncamento, append, seek negativo/overflow, escrita
parcial e liberacao dos descritores/cwd por PID.

`check-vfs` inicia seis VMs: autotestes em BIOS e UEFI com 128/512 MiB, mais
comandos do terminal em cada firmware. O autoteste usa dois processos com
duas threads escritoras cada para verificar descritores isolados por PID,
offsets compartilhados por threads e integridade de registros. Usa ainda
dois processos fazendo append no mesmo arquivo e quatro ciclos de kill de
um processo com arquivo aberto e cwd dentro do volume. Um callback atrasa
deliberadamente a escrita por oito ticks para confirmar que o PIT continua
ativo enquanto a troca de tarefa fica adiada ate liberar o VFS.

Depois de exit/kill e unmount, o teste exige os mesmos numeros de descritores,
sessoes, montagens, paginas fisicas e alocacoes de heap da linha de base.
O terminal e o desligamento ACPI devem continuar funcionando. Resultados:
`build/vfs-results.json` e `build/vfs-*.log`. O autoteste de boot pode ser
acionado com `cmdline: selftest=vfs` na configuracao do Limine.

## API e caminhos

`kernel/include/vfs.h` expoe a API do kernel. `vfs_open(path, flags, &fd)`
aceita `VFS_READ`, `VFS_WRITE`, `VFS_CREATE`, `VFS_EXCLUSIVE`, `VFS_TRUNCATE`,
`VFS_APPEND` e `VFS_OPEN_DIR`. Diretorios exigem READ|OPEN_DIR e sao percorridos
com `vfs_readdir`; o indice avanca por entrada e pode ser reiniciado com seek
SET/0. Read/write em diretorios sao recusados.

Read/write retornam `{error, count}`. A posicao avanca pela quantidade de
bytes realmente transferida, inclusive quando uma escrita e parcial e retorna
erro. EOF retorna sucesso com zero bytes. Acesso sem a flag adequada, descritor
fechado/inexistente ou buffer invalido sao recusados. O chamador deve fornecer
memoria mapeada, com permissoes adequadas, durante toda a operacao; os testes
de tamanho/overflow nao substituem a validacao de ponteiros de usuario.

`vfs_seek` aceita SET/CUR/END, offset assinado e retorna a nova posicao; nao
aceita resultado negativo nem acima de INT64_MAX. E permitido posicionar alem
do EOF; RAMFS zera o intervalo quando uma escrita posterior aumenta o arquivo.
Append seleciona EOF e escreve sob o mesmo lock, mesmo depois de um seek.
Cada open tem sua propria posicao. Nao ha dup/fork nesta etapa.

Um processo recebe cwd `/` quando usa o VFS pela primeira vez. Suas threads
compartilham cwd e descritores, e ambos sao liberados ao encerrar o processo
por saida normal ou kill. Encerrar apenas uma thread preserva os descritores
do processo. Os numeros 0/1/2 permanecem reservados para a ABI de console;
descritores VFS usam 3..31. A API nao recebe um PID arbitrario: usa sempre o
processo atual. As syscalls existentes de console permanecem independentes;
syscalls de arquivos ainda nao fazem parte desta etapa.

O resolvedor percorre diretorios reais, sem simplificar caminhos antes de
consultar os nodes: `arquivo/..` retorna `nao e diretorio`. `..` na raiz global
permanece na raiz; na raiz de um volume montado, retorna ao pai do ponto de
montagem. Uma barra final exige diretorio. Links simbolicos, hard links,
permissoes Unix ficam para outras etapas. Rename/move e exclusao agora sao
expostos por `vfs_rename`/`vfs_remove`, com recusa de referencias ativas,
destino existente, ciclos e passagem entre volumes. RAMFS e FAT32 gravavel
implementam essas operacoes. `vfs_truncate` redimensiona arquivos e
`vfs_sync`/`vfs_sync_all` sincronizam um ou todos os volumes.

## Montagem e vida util

O backend fornece root e operacoes de lookup/create/read/write/truncate/
readdir/sync. Nodes possuem nome, tipo, tamanho, pai e volume. O backend e
dono dessas estruturas e deve mante-las estaveis enquanto referenciadas.
Remocao e liberacao de um node so ocorrem depois que o VFS verifica ausencia
de FDs/cwd/montagens nele ou em sua arvore. A mesma
instancia de volume so pode estar montada uma vez. Os dados podem continuar
existindo apos unmount; liberar/reinicializar o backend e responsabilidade
de seu dono, apenas quando nao estiver montado.

Unmount exige a raiz exata de uma montagem, nao aceita a raiz global, e recusa
descritores abertos, cwd de qualquer processo ou montagens filhas. Antes de
remover a montagem chama sync; se sync falhar, o volume permanece acessivel.
Backends gravaveis sem sync nao podem ser desmontados. Nao existe unmount
forcado. O reap do processo apenas solta referencias em memoria: nao chama
callbacks nem tenta executar I/O com interrupcoes desabilitadas.

## Concorrencia e limites

Na arquitetura atual de um CPU, o VFS suspende a preempcao durante a operacao
completa e mantem as interrupcoes habilitadas. Isso protege tabelas de FDs,
cwd, montagens, offsets e callbacks, incluindo a escolha de EOF em append.
O lock e liberado antes de permitir uma troca de tarefa. A liberacao de
processos ocorre no reaper do escalonador, com IRQs desabilitadas, sem I/O.

Callbacks nao podem fazer yield/sleep, chamar o VFS recursivamente ou reter
buffers do chamador. Podem usar armazenamento sincrono que mantenha as IRQs
habilitadas, como ATA PIO; o tempo de bloqueio depende do backend. Chamadas
normais com IF=0 retornam erro de contexto. Uso em IRQ/NMI e SMP nao e suportado.

Limites: oito montagens (incluindo raiz), 32 processos com sessao, 29 FDs por
processo, nomes de 31 bytes, caminhos de ate 255 bytes e I/O de ate
64 KiB por chamada. RAMFS tem 32 nodes por volume (incluindo raiz), arquivos
gravaveis de ate 4096 bytes e armazenamento estatico. Arquivos imutaveis de
boot referenciam os bytes reservados pelo Limine e podem exceder 4096 bytes.
Esgotamento retorna erro, sem
sobrescrever nodes ou tabelas. Nao ha sistema de arquivos persistente nesta etapa.
