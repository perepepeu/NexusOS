# ABI de chamadas de sistema — v1

Arquitetura x86_64, uma CPU. A entrada e `INT 0x80` em CPL3; a CPU muda para
a pilha de kernel via TSS.RSP0. O retorno usa IRETQ com validacao dos seletores,
enderecos de retorno e flags. O opcode `SYSCALL` nao e a entrada desta ABI.

RAX recebe o numero da chamada e devolve um inteiro de 64 bits com sinal.
RDI, RSI e RDX carregam os tres primeiros argumentos. Outros GPRs sao
preservados. O kernel preserva flags de usuario validas (incluindo DF),
mantendo IOPL=0; programas nao podem desabilitar o PIT. Registradores FPU/SIMD
continuam indisponiveis nesta etapa. Nao ha libc nem variavel global errno:
um retorno negativo e o proprio erro.

| Numero | Chamada | RDI | RSI | RDX | Retorno |
|---|---|---|---|---|---|
| 0 | exit | status, 32 bits inferiores | — | — | Nao retorna |
| 1 | yield | — | — | — | 0 |
| 2 | getpid | — | — | — | PID atual |
| 3 | gettid | — | — | — | TID atual (extensao existente) |
| 4 | ticks | — | — | — | Ticks do PIT, 100 Hz (extensao existente) |
| 5 | write | fd (1/2 ou arquivo) | buffer fonte | bytes | Bytes escritos |
| 6 | read | fd (0 ou arquivo) | buffer destino | bytes | Bytes lidos; 0 no EOF de arquivo |
| 7 | sleep | milissegundos | — | — | 0 |
| 8 | open | caminho NUL | flags | — | FD 3..31 |
| 9 | close | fd | — | — | 0 |
| 10 | seek | fd | offset int64 | SET/CUR/END | Nova posicao |
| 11 | stat | caminho NUL | user_stat* | — | 0 |
| 12 | mkdir | caminho NUL | — | — | 0 |
| 13 | unlink | caminho NUL | 0=arquivo, 1=diretorio vazio | — | 0 |
| 14 | readdir | fd de diretorio | user_dirent* | — | 1=entrada, 0=fim |
| 15 | chdir | caminho NUL | — | — | 0 |
| 16 | getcwd | buffer | capacidade | — | Bytes incluindo NUL |
| 17 | sync | caminho ou NULL=todos | — | — | 0 |

Os numeros existentes 0..7 foram preservados; 8..17 estendem a ABI.
`kernel/include/syscall_abi.h` e a fonte das constantes C/NASM; o Makefile gera
o include NASM. `user/include/syscall.h` fornece wrappers C freestanding.
Todas as chamadas da tabela estao implementadas.

Buffers devem estar inteiramente na metade baixa, em paginas USER presentes.
O destino de read tambem exige permissao WRITE efetiva em todos os niveis.
O kernel verifica todo o intervalo antes de copiar, inclusive cruzamentos de
paginas e overflow. A copia do loader, que pode popular paginas RX, e separada
da copia de syscalls: o usuario nao ganha permissao de escrever em codigo.

O contrato de I/O limita uma chamada a 4096 bytes. fd invalido retorna -9
(EBADF); tamanho acima do limite ou duracao invalida, -22 (EINVAL); ponteiro
invalido ou sem permissao, -14 (EFAULT); teclado indisponivel, -19 (ENODEV);
numero desconhecido, -38 (ENOSYS).
A ordem e fd, tamanho, ponteiro. Comprimento zero retorna 0 para um fd valido,
sem acessar o ponteiro, mesmo que ele seja nulo ou invalido. Dados sao bytes,
sem terminador NUL obrigatorio; strings nao sao formatos de log.

sleep aceita 0..INT64_MAX milissegundos. Valores positivos sao arredondados
para cima a ticks de 10 ms, com um tick extra para cobrir a fase corrente do
PIT, sem overflow na soma. Zero equivale a yield.
O prazo usa o PIT; a tarefa bloqueia e outras tarefas continuam executando.

read no fd 0 usa o teclado PS/2 compartilhado com o terminal, sem edicao de linha nem
eco automatico. Deve aguardar pelo menos um caractere de tecla pressionada e
devolver os disponiveis ate o limite solicitado, podendo retornar menos.
O chamador deve repetir para completar mensagens. A copia usa um buffer de
kernel de 256 bytes, portanto uma leitura pode retornar no maximo 256 bytes,
mesmo que o pedido valido seja maior. Parametros invalidos nao consomem teclas.
O buffer de usuario e revalidado apos o despertar. No modelo atual, cada PID
possui uma thread e nao ha syscalls que alterem mapas enquanto read bloqueia.
Arquivos usam a tabela VFS por PID; nao ha pipes nem sinais. Um programa interativo deve
ser executado em primeiro plano, com o terminal pai aguardando seu encerramento.

`sysdemo` no terminal executa um programa C real em CPL3, le uma linha de ate
64 bytes sem eco, mostra a linha e termina. O terminal pai aguarda por wait;
ao terminar, volta ao prompt e mostra status=0.

`make check-syscalls` compila os programas C freestanding, executa os perfis
write/sleep e read em BIOS/UEFI com 128, 512 e 5120 MiB e injeta teclas pelo
QMP somente apos confirmar BLOCKED/INPUT. A tarefa testemunha continua rodando
durante read/sleep. Sao verificados registradores e CF/DF/IF, erros negativos,
buffers RX/RW, paginas ausentes, intervalos cruzados, zero/overflow/limites,
NUL na serial, leituras curtas, ausencia de consumo em falhas, kill durante
read, retorno ao terminal e contadores de memoria restaurados. O alvo inclui
os testes ASan/UBSan da validacao/copia de ponteiros.

Os programas desta suite de syscalls ainda usam imagens planas embutidas.
O carregador de [programas ELF64](elf64.md) tambem executa arquivos completos
fornecidos como modulos Limine e agora le [ELFs diretamente do disco](fs-integration.md), usando a mesma ABI.

## Arquivos e seguranca de ponteiros

Flags de open: READ=1, WRITE=2, CREATE=4, EXCLUSIVE=8, TRUNCATE=16, APPEND=32,
DIRECTORY=64. READ ou WRITE e obrigatorio. Diretorios exigem READ|DIRECTORY.
Offset de seek e int64; whence SET=0, CUR=1, END=2. Diretorios so aceitam SET/0
para reiniciar readdir. unlink com flag 1 tambem e exposto como `sys_rmdir`.

`struct user_stat` tem 16 bytes: size uint64, type uint32, reserved uint32.
`struct user_dirent` tem 48 bytes: esses mesmos campos e name[32], terminado
em NUL. Type FILE=1, DIR=2. Reserved e bytes apos o nome sao zero; enderecos
e padding de estruturas internas nao sao exportados. EOF de readdir nao
altera a estrutura de saida. O ponteiro de saida e validado antes de consumir
uma entrada, inclusive quando o arquivo esta no fim.

Caminhos possuem no maximo 255 bytes mais NUL e sao copiados integralmente
antes de open/mkdir/unlink/etc. Uma string que cruza pagina ausente nao causa
O_TRUNC parcial. I/O de arquivo faz preflight integral dos 4096 bytes maximos
e usa um buffer do kernel por chamada; write copia os dados antes de tocar no
VFS. Parametros invalidos nao avancam offsets nem consomem entradas.

FDs pertencem ao PID, sao compartilhados por suas threads e comecam em 3.
Outros processos nao podem usar o mesmo numero para acessar a tabela alheia.
Os handles de console 0/1/2 permanecem fixos: close/seek neles retorna EBADF.
Exhaustao da tabela retorna ENOSPC. Modos READ/WRITE, mounts somente leitura,
atributos FAT e permissoes USER/WRITE de memoria sao aplicados. Ainda nao ha
UID/GID ou permissoes Unix no sistema de arquivos.

Erros adicionais: ENOENT=2, EIO=5, EBUSY=16, EEXIST=17, EXDEV=18,
ENOTDIR=20, EISDIR=21, ENOSPC=28, EROFS=30, ENAMETOOLONG=36 e
ENOTEMPTY=39. I/O no modo errado de um FD retorna EBADF. Um processo novo
comeca com cwd `/`; pode mudar seu proprio cwd com chdir. Exit, kill e
excecoes liberam todos os FDs/cwd pelo reaper, sem I/O com IRQs desabilitadas.

`make check-fs-syscalls` executa dois ELF de teste em Ring 3, em BIOS/UEFI,
com RAMFS. `make check-fs-integration` repete o fluxo no FAT32, carregando os
ELFs do disco, e cobre comandos, concorrencia, cleanup e recuperacao.
