# FAT32 — leitura

- [x] Interpretar setor de boot e BPB.
- [x] Validar geometria e identificar FAT32 pelo numero de clusters.
- [x] Localizar setores reservados, FATs e regiao de dados.
- [x] Interpretar FSInfo e validar suas dicas.
- [x] Localizar a raiz pelo BPB_RootClus.
- [x] Interpretar entradas de diretorios, incluindo diretorios fragmentados.
- [x] Suportar nomes curtos 8.3.
- [x] Tratar entradas LFN com seguranca, expondo o alias 8.3.
- [x] Percorrer e validar cadeias de clusters.
- [x] Ler arquivos fragmentados, com seek e EOF.
- [x] Detectar referencias invalidas e circulares.

O driver usa a camada de blocos e fornece um backend somente de leitura ao
VFS. Nao modifica FATs, FSInfo, timestamps, entradas 8.3 ou registros LFN.
Esse e o comportamento de `mountfat`; a [etapa de escrita](fat32-write.md)
adiciona `mountfatrw` para habilitar mutacoes explicitamente.
Arquivos de nomes longos sao acessados pelo alias curto, como `LONGFI~1.TXT`;
esta etapa nao reconstrui o nome UTF-16 dos registros LFN.

## Usar no NexusOS

Com um disco ATA contendo FAT32 a partir de seu primeiro setor:

```text
disks
mountfat ata0 /mnt
fatinfo
ls /mnt
cat /mnt/README.TXT
cd /mnt/SUB
ls .
cd /
umount /mnt
```

O diretorio de montagem precisa existir. Para um volume cujo intervalo ja e
conhecido, informe LBA inicial e quantidade de setores, em decimal:

```text
mountfat ata0 /mnt 2048 131072
```

Esses valores sao um exemplo, nao uma descoberta automatica da particao.
O comando nao interpreta MBR/GPT: o intervalo passado deve corresponder ao
volume FAT32. A geometria do BPB deve caber integralmente nele. `fatinfo`
mostra geometria, FAT selecionada, FSInfo e o ultimo erro do backend.

O VFS rejeita escrita, criacao e truncamento nesse volume. Desmontagem com
FDs abertos, cwd ativo ou montagem dependente retorna `volume ocupado`.
Depois de desmontar, a instancia do driver pode ser reutilizada com seguranca.
O terminal possui dois slots FAT32; a consulta de montagens e a selecao/reset
dos slots sao serializados antes de substituir nodes existentes.

## Geometria e metadados

O parser usa leituras little-endian explicitas, sem dereferenciar structs
desalinhadas do disco. Confere assinatura, formato FAT32 dos campos BPB,
versao zero, capacidade da FAT, cluster raiz e limites das regioes. O texto
`FAT32` do setor nao determina o tipo.

Suporta setores logicos de 512, 1024, 2048 ou 4096 bytes, iguais ao tamanho
de setor do dispositivo, e clusters de ate 32 KiB. Identifica ate quatro FATs.
Com espelhamento habilitado le a FAT 0; sem espelhamento usa a FAT ativa
indicada no BPB, apos validar seu indice. Nao tenta reconciliar copias
divergentes nem recuperar automaticamente pelo boot sector de backup.

FSInfo e consultado dentro da regiao reservada. Suas assinaturas, contagem de
livres e dica de proximo cluster sao validadas separadamente. Assinaturas
invalidas ou dicas desconhecidas/fora de faixa nao sao usadas como verdade
sobre alocacao e nao impedem ler um volume cuja FAT e valida. Falha real de
I/O em metadados e propagada como erro.

Diretorios usam registros de 32 bytes. Entradas livres, apagadas, rotulos,
registros `.`/`..` e slots LFN sao tratados separadamente. O VFS resolve
`.`/`..` pelos nodes. Nomes 8.3 usam comparacao sem distinguir caixa ASCII,
e os indicadores de caixa do NT sao respeitados. Bytes OEM sao preservados,
incluindo o escape inicial 05/E5, sem conversao de codepage/Unicode.

## Cadeias e erros

Os valores da FAT sao mascarados para 28 bits. Uma cadeia deve conter apenas
clusters de dados dentro do volume e terminar em EOC. Clusters livres,
reservados, defeituosos e referencias fora da geometria geram erro. A raiz e
validada ao abrir o volume; arquivos/subdiretorios, antes de materializar seus
nodes. Arquivos precisam ter a quantidade de clusters correspondente ao
tamanho declarado, nesta implementacao de validacao estrita.

A deteccao de ciclos usa Brent, com memoria constante e percurso progressivo,
incluindo a parte da cadeia posterior ao EOF declarado ou a um marcador de fim
do diretorio. Links de subdiretorio para um ancestral tambem sao rejeitados.
Um arquivo corrompido retorna erro ao VFS; os demais arquivos e o terminal
continuam disponiveis. Uma falha no meio da leitura informa os bytes ja lidos,
e o VFS avanca a posicao apenas por esses bytes.

Limites: 128 nodes materializados por volume, 131072 clusters por validacao
de cadeia e 65536 registros por busca de diretorio. Ao atingir um limite,
retorna erro de recursos/percurso, sem continuar indefinidamente. O cache de
FAT usa um setor; dados usam outro buffer e a leitura sequencial tem cursor de
cluster por node. O conteudo do disco deve ficar estavel durante a montagem;
nao modificar a imagem por fora enquanto montada. As operacoes sao sincronas,
sob a protecao de concorrencia do VFS para um CPU.

## Testes no WSL

```sh
sudo apt-get install mtools  # se ainda nao estiver instalado
make check-fat32
```

`make check-fat32-unit` executa somente os testes ASan/UBSan. Eles cobrem BPB,
tipo, geometria, FAT ativa, FSInfo invalido/desconhecido, setores 512/4096,
8.3/LFN, arquivos vazios, fragmentacao, seek, EOF, erro parcial e cadeias
livres/reservadas/defeituosas/curtas/longas/circulares.

O teste integrado cria arquivos novos em `/tmp`, formata com `mformat`, copia
com `mcopy` e verifica a leitura do arquivo fragmentado com `mtype`, do mtools.
Depois inicia oito VMs: BIOS/UEFI para volume normal, BPB invalido, ciclo na
raiz e volume no LBA 2048 com setores de guarda. Verifica leitura integral de
8197 bytes fragmentados, hashes, subdiretorio, arquivo vazio, alias LFN, erro
de um arquivo circular, readonly, unmount ocupado e remount.

Os contadores de escrita do QEMU devem ficar em zero e o SHA-256 completo da
imagem deve permanecer identico. As VMs UEFI usam uma copia descartavel de
OVMF_VARS_4M.fd como NVRAM, separada do disco testado. Isso evita que a
configuracao com somente OVMF_CODE persista variaveis no FAT antes de entrar
no kernel; esse comportamento foi confirmado com breakpoint em kmain.
Nenhum disco existente do usuario e recebido/formatado pelo teste.

Resultados: `build/fat32-results.json`, `build/fat32-*.log`.

Referencia de formato: [Microsoft FAT32 File System Specification 1.03](https://www.pcjs.org/documents/papers/microsoft/MS_FAT_OVERVIEW_103-2000-12-06.pdf).
