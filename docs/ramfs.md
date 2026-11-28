# RAMFS e inicializacao

- [x] Sistema de arquivos em RAM.
- [x] Diretorios e arquivos na memoria.
- [x] Leitura, escrita, EOF, seek, append e truncamento.
- [x] Integracao ao VFS, incluindo cwd e descritores por PID.
- [x] Modulos de inicializacao do Limine acessiveis pelo VFS.

A raiz `/` e o volume `/tmp` usam o RAMFS gravavel da etapa anterior. Durante
o boot, um terceiro volume RAMFS, somente de leitura, e montado em
`/boot/modules`. Com a configuracao atual, ele contem `alpha`, `beta`, `fault`
e `busy`: os mesmos arquivos ELF64 fornecidos pelo Limine ao catalogo de
programas. A integracao funciona com arquivos de dados tambem; nao exige ELF.
Esta etapa usa os modulos Limine como fonte dos arquivos de inicializacao.

## Experimentar

No NexusOS:

```text
ls /boot/modules
vfsinfo
mkdir /tmp/teste
write /tmp/teste/nota ola RAMFS
append /tmp/teste/nota segunda linha
cat /tmp/teste/nota
write /boot/modules/alpha tentativa
```

O ultimo comando retorna `somente leitura`. O volume de modulos nao permite
criacao de arquivos/diretorios nem truncamento. Os modulos atuais sao binarios;
para consulta-los, use listagem ou a API `vfs_stat_path`/`vfs_read`. A leitura
pelo VFS nao altera o catalogo nem a execucao dos programas existentes.

Arquivos criados no RAMFS sao volateis e desaparecem ao reiniciar.

## Importacao e limites

`bootfs_populate` inicializa um volume ainda nao montado e percorre os modulos
da resposta Limine. Usa `module_string` como nome; se estiver vazio/ausente,
usa o ultimo componente de `limine_file.path`. Os nomes sao copiados para os
nodes, enquanto os bytes do arquivo continuam na memoria reservada pelo
bootloader. Assim, os ELF atuais de aproximadamente 13 KiB ficam acessiveis
integralmente, sem copia ou truncamento no limite de 4 KiB do RAMFS gravavel.

Sao aceitos nomes de 1 a 31 bytes com letras ASCII, numeros, `_`, `-` e `.`.
`.` e `..`, barras, caminhos de escape, nomes longos e duplicados sao recusados.
A primeira entrada valida de um nome permanece; entradas seguintes nao a
substituem. Arquivos vazios sao validos. Ponteiro nulo com tamanho nao zero,
overflow do intervalo e arquivos maiores que 16 MiB sao rejeitados.

O importador inspeciona no maximo 64 entradas; o volume comporta 31 arquivos
mais a raiz. Os limites geram contadores/diagnostico e nao escrevem alem dos
arrays. Se a resposta de modulos estiver ausente, o boot continua com o volume
vazio e somente de leitura. Um erro para criar/montar o volume VFS gera panic
de inicializacao; uma entrada de modulo rejeitada nao impede as demais.

`ramfs_import_file` e uma API para volumes nao montados, usada durante a
inicializacao. Arquivos importados tambem recusam write/truncate no backend,
mesmo se o volume ainda nao tiver sido marcado como readonly. Os demais
arquivos RAMFS continuam com 4096 bytes por arquivo, preenchimento com zeros
ao crescer, escritas parciais com erro de capacidade e os contratos do VFS.

## Vida util da memoria

Os dados importados sao referencias imutaveis, nao copias pertencentes ao
processo. O PMM atual aloca apenas regioes USABLE; as regioes de kernel/modulos
Limine permanecem reservadas e mapeadas durante toda a execucao. O catalogo ELF
tambem referencia os dados originais. Desmontar o volume solta apenas a montagem
VFS e nao libera essa memoria. Antes de implementar reclamacao de memoria de
boot, sera necessario preservar/copiar os dados ainda utilizados por ambos.

## Validar no WSL

```sh
make check-ramfs
# Somente testes unitarios:
make check-ramfs-unit check-bootfs-unit check-vfs-unit
```

Os testes com ASan/UBSan verificam importacao de arquivo maior que 4 KiB,
leitura integral em varios blocos, seek pelo final, EOF, arquivos vazios,
write/truncate recusados, nomes inseguros, duplicatas, fallback de nome,
resposta ausente/inconsistente e limite de inspecao.

A integracao inicia seis VMs, em BIOS e UEFI: modulos normais, resposta ausente
e mistura de entradas validas/invalidas/duplicadas. O guest compara cada byte
lido pelo VFS com os dados originais, verifica tamanho, EOF e ultima posicao,
e gera um hash FNV-1a de 32 bits. O host compara tamanhos/hashes com os ELF
originais da build. Os cenarios tambem exercitam o RAMFS gravavel no terminal,
recusam alteracao dos modulos, verificam ausencia de FDs vazados e desligam
normalmente pelo ACPI.

Resultados: `build/ramfs-results.json`, `build/ramfs-*.log`. O autoteste pode
ser acionado com `cmdline: selftest=bootfs` na configuracao Limine.
