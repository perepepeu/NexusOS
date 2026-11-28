# FAT32 — escrita

- [x] Localizar clusters livres pela FAT, com contagem reconstruida.
- [x] Alocar e liberar clusters.
- [x] Atualizar FATs espelhadas ou somente a FAT ativa.
- [x] Criar arquivos.
- [x] Criar diretorios com `.` e `..`.
- [x] Escrever conteudo, com crescimento e intervalos preenchidos com zero.
- [x] Append ao final do arquivo.
- [x] Truncar e redimensionar arquivos.
- [x] Renomear e mover dentro do mesmo volume.
- [x] Excluir arquivos e diretorios vazios.
- [x] Atualizar tamanho, primeiro cluster, atributos e timestamps.
- [x] Sincronizar dados e metadados com flush.
- [x] Tratar disco cheio sem sobrescrever clusters alocados.

## Usar

`mountfat` continua somente para leitura. Escrita exige `mountfatrw`:

```text
mountfatrw ata0 /mnt
mkdir /mnt/DOCS
write /mnt/DOCS/NOTA.TXT primeira linha
append /mnt/DOCS/NOTA.TXT segunda linha
cat /mnt/DOCS/NOTA.TXT
truncate /mnt/DOCS/NOTA.TXT 8
mv /mnt/DOCS/NOTA.TXT /mnt/OUTRO.TXT
rm /mnt/OUTRO.TXT
rmdir /mnt/DOCS
sync /mnt
umount /mnt
```

Ambos os comandos de montagem aceitam `[LBA SETORES]` para um intervalo de
volume conhecido. Volumes sobrepostos no mesmo dispositivo nao podem ser
montados simultaneamente se um deles for gravavel. O dispositivo deve oferecer
escrita e flush; a FAT deve indicar volume limpo e sem erro anterior.

Criacao e novos nomes usam ASCII 8.3, em caixa alta. Arquivos LFN existentes
podem ser escritos pelo alias. Atualizar dados/tamanho preserva os LFN;
renomear para 8.3 ou excluir remove o conjunto associado, validando ordem e
checksum. Novos nomes curtos tambem sao comparados com nomes longos ASCII
existentes. Conjuntos LFN desconhecidos/orfaos impedem mutacoes de nomes no
diretorio, em vez de serem reutilizados inadvertidamente.

`truncate CAMINHO` reduz a zero; com BYTES, reduz ou aumenta, zerando bytes
novos. `write` abre com truncamento e substitui o conteudo; `append` preserva
o existente. Ambos os comandos acrescentam newline ao texto, como antes.
`mv` nao substitui um destino existente e nao copia entre volumes. Arquivos
abertos, cwd ativo ou montagens na arvore impedem renomear/excluir. Diretorio
nao vazio e recusado. RAMFS tambem implementa as novas operacoes do VFS.

## Alocacao e metadados

Antes de habilitar escrita, o driver percorre a FAT, reconta clusters livres
e confere as copias espelhadas nos 28 bits significativos. FSInfo nao prova
que ha espaco. As atualizacoes preservam os quatro bits altos individualmente
em cada FAT e atualizam FSInfo principal/backup validos. FATs nao ativas ficam
intactas quando o espelhamento esta desabilitado.

Clusters e setores de metadados sao reservados/preparados em memoria antes
de tocar nos dados. Falta de capacidade retorna zero bytes e erro de espaco,
sem estender o arquivo ou reaproveitar clusters ocupados. Disco cheio ainda
permite sobrescrever bytes ja alocados. Truncar/excluir libera clusters.

Clusters novos e gaps de seek sao zerados, incluindo a parte nao utilizada do
ultimo cluster antigo. Diretorios crescem por clusters, com terminadores
corretos. Mover um diretorio atualiza `..` no disco. Nodes excluidos podem ser
reutilizados. Escrita/truncamento atualizam tamanho, primeiro cluster, archive
e data de modificacao; rename/move preservam datas do arquivo. O atributo
somente leitura impede mutacoes.

Datas vem do RTC CMOS em formato DOS, com resolucao de dois segundos,
conforme o relogio da VM. Sem leitura valida, usa 1980-01-01 00:00.

## Sync e falhas

Antes da primeira mutacao apos sync, o bit de volume limpo e removido e
persistido. Dados recebem flush antes de publicar crescimento na FAT e no
diretorio. Na reducao/exclusao, publica o tamanho menor ou a remocao antes de
liberar a cadeia. FSInfo e escrito por ultimo.

`sync [CAMINHO]` sincroniza o volume; sem argumento, todos. Unmount sincroniza
antes de remover a montagem. Reboot e shutdown tambem sincronizam antes de
acionar o hardware. O bit limpo e publicado depois do flush dos metadados;
uma escrita posterior volta a remover esse bit antes de modificar dados.

Erro de escrita/flush bloqueia mutacoes, marca a instancia somente leitura e
faz sync/unmount falharem. Leituras dessa instancia tambem sao bloqueadas para
nao expor estado parcial como consistente. O kernel e outros volumes continuam
funcionando. Nao ha repeticao automatica de escrita nem falsa marcacao de
volume limpo depois de um erro.

Os buffers formam uma transacao em RAM, nao um journal em disco. Falha de
hardware ou falta de energia pode deixar dados parciais, clusters orfaos ou
rename/move incompleto. O volume precisa ser verificado por uma ferramenta
FAT antes de voltar a escrita. A ordenacao/bit sujo ajudam na recuperacao,
mas nao tornam rename entre diretorios atomico a falta de energia. Um erro
nao promete rollback do que ja chegou ao disco.

## Limites

A escrita herda limites da leitura/VFS e adiciona: 1048576 clusters no volume
para verificacao inicial, 256 clusters novos e 64 setores de metadados por
operacao. Excesso retorna erro de recursos antes da fase de dados. Escritas
grandes podem ser divididas; reducao/exclusao que exceda o limite de metadados
e recusada. Arquivos seguem UINT32_MAX, dentro dos demais limites do driver.
O volume deve ficar exclusivo e sem alteracoes externas. A verificacao inicial
nao substitui um fsck global. Nomes longos novos, journal e SMP nao estao
implementados nesta etapa.

## Validacao

No WSL:

```sh
sudo apt-get install mtools dosfstools
make check-fat32-write
```

`make check-fat32-write-unit` usa ASan/UBSan: alocacao, FATs, FSInfo,
timestamps, dados/zeros/append/truncate, move/dotdot, exclusoes, crescimento
de diretorios, reutilizacao, disco cheio, um cluster livre, dicas obsoletas,
bits reservados, LFN e falhas em varias fases da escrita/flush.

A integracao usa apenas imagens novas em `/tmp`, formatadas pelo mtools.
Em BIOS/UEFI, verifica escrita, comandos e outra VM para persistencia. Outras
VMs usam disco realmente cheio por um arquivo valido e I/O com erro injetado
pelo blkdebug. O autoteste destrutivo exige uma assinatura descartavel antes
de mutar; essa exigencia e so do teste, nao da API normal.

O host compara dados por mtype/mdir, FATs e contadores, e executa
`fsck.fat -n -v`. Casos normais devem estar consistentes/limpos; o erro
injetado deve manter o bit sujo. OVMF usa NVRAM separada do disco FAT.
Resultados: `build/fat32-write-results.json`, `build/fat32-write-*.log` e
`build/fat32-fsck-*.log`. Regressao: `make check-fat32` e `make check-vfs`.

Referencia: [Microsoft FAT32 File System Specification 1.03](https://www.pcjs.org/documents/papers/microsoft/MS_FAT_OVERVIEW_103-2000-12-06.pdf).
