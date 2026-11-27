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
| 5 | write | fd (1 ou 2) | buffer fonte | bytes | Bytes escritos |
| 6 | read | fd (0) | buffer destino | bytes | Bytes lidos; leituras curtas permitidas |
| 7 | sleep | milissegundos | — | — | 0 |

Os numeros 0..4 sao compativeis com os programas iniciais de Ring 3.
`kernel/include/syscall_abi.h` e a fonte das constantes C/NASM; o Makefile gera
o include NASM. `user/include/syscall.h` fornece wrappers C freestanding.
write/sleep estao implementadas; read permanece reservada ate a etapa de entrada.

Buffers devem estar inteiramente na metade baixa, em paginas USER presentes.
O destino de read tambem exige permissao WRITE efetiva em todos os niveis.
O kernel verifica todo o intervalo antes de copiar, inclusive cruzamentos de
paginas e overflow. A copia do loader, que pode popular paginas RX, e separada
da copia de syscalls: o usuario nao ganha permissao de escrever em codigo.

O contrato de I/O limita uma chamada a 4096 bytes. fd invalido retorna -9
(EBADF); tamanho acima do limite ou duracao invalida, -22 (EINVAL); ponteiro
invalido ou sem permissao, -14 (EFAULT); numero desconhecido, -38 (ENOSYS).
A ordem e fd, tamanho, ponteiro. Comprimento zero retorna 0 para um fd valido,
sem acessar o ponteiro, mesmo que ele seja nulo ou invalido. Dados sao bytes,
sem terminador NUL obrigatorio; strings nao sao formatos de log.

sleep aceita 0..INT64_MAX milissegundos. Valores positivos sao arredondados
para cima a ticks de 10 ms, com um tick extra para cobrir a fase corrente do
PIT, sem overflow na soma. Zero equivale a yield.
O prazo usa o PIT; a tarefa bloqueia e outras tarefas continuam executando.

read usa o teclado PS/2 compartilhado com o terminal, sem edicao de linha nem
eco automatico. Deve aguardar pelo menos um caractere de tecla pressionada e
devolver os disponiveis ate o limite solicitado, podendo retornar menos.
O chamador deve repetir para completar mensagens. Nao ha arquivos, pipes,
EOF, sinais ou tabela geral de descritores ainda. Um programa interativo deve
ser executado em primeiro plano, com o terminal pai aguardando seu encerramento.
