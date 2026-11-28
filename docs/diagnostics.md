# Testes e diagnostico de processos

ps lista PID/PPID, estado, threads, ticks acumulados, status de saida, nome e
modo R0/R3. kill PID encerra o grupo e libera os recursos fora da CPU;
Idle/PID 0 e kernel/PID 1 sao protegidos. wait PID coleta o status e remove
os descritores TERMINATED retidos para o pai. Um descritor aguardando wait
nao significa que a stack ou as paginas de usuario ainda estejam alocadas.

run NOME carrega um ELF do catalogo programs e devolve o PID. top mede uma
janela de pelo menos um segundo do PIT, inclui kernel/Idle, ordena os PIDs por
CPU e volta ao prompt. CPU e exibida com uma casa decimal; representa ticks
da CPU guest atribuidos ao PID, inclusive suas syscalls. Nao mede CPU do host.
O PID que terminou/coletou antes do segundo snapshot pode nao aparecer na
tabela. A amostra e unica: execute top novamente para atualizar.

Teste manual:

```text
programs
run busy
run busy
ps
top
kill PID_A
wait PID_A
kill PID_B
wait PID_B
run fault
wait PID_FAULT
elfdemo
pmm
vmm
heap
```

Substitua PID_A/PID_B/PID_FAULT pelos numeros mostrados por run. busy nao chama
sleep/yield e termina sozinho apos 30 segundos se nao for encerrado. Com dois
busy, top deve mostrar uso positivo de CPU para ambos. kill/wait devolve -9.
fault faz uma leitura NULL prevista, deve terminar com -270 (page fault), e o
kernel/terminal devem continuar ativos. elfdemo deve retornar alpha=0 beta=0.
Compare pmm/vmm/heap antes e depois: os recursos devem voltar ao baseline.

```sh
make check-diagnostics
make check-scheduler
make check-user
make check-elf
```

check-diagnostics roda BIOS/UEFI com 128, 512 e 5120 MiB. Cada boot executa tres
ciclos: um app falha enquanto outro segue vivo; um novo app e criado apos a
falha; ambos os apps saudaveis terminam; outro e encerrado durante sleep.
PMM, VMM, heap, PCB e TCB retornam aos valores iniciais em cada ciclo. Pelo
QMP, os comandos ps/kill/run/top sao exercitados com dois busy, kill protegido,
PID invalido/overflow, fault, wait e nova execucao ELF apos a falha.

check-scheduler prova preempcao de tarefas sem yield e preservacao de GPRs e
flags. check-user/check-elf verificam isolamento, permissoes, guardas e falhas
confinadas ao processo. Os testes de vazamento cobrem os ciclos exercitados;
nao substituem testes de duracao para futuras funcionalidades.
