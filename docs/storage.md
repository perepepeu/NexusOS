# Armazenamento: dispositivos de bloco e ATA PIO

Implementacao posterior a v0.3, ainda sem nova versao publicada.

- [x] Camada generica de dispositivos de bloco (`block.h`, `block.c`).
- [x] Descoberta PCI de controladores de armazenamento e selecao de IDE.
- [x] Driver ATA PIO, enderecamento LBA28 e LBA48.
- [x] IDENTIFY: capacidade, tamanho logico dos setores e modelo.
- [x] Leitura de blocos por LBA.
- [x] Escrita de blocos por LBA.
- [x] Flush explicito do cache do disco.
- [x] Timeouts, diagnostico de erros e isolamento do canal apos falha.
- [x] Rejeicao de setores fora dos limites antes de acessar o hardware.

## Usar e testar

No Ubuntu/WSL, dentro da pasta do projeto:

```sh
make check-storage
```

O teste compila a ISO e executa testes unitarios com AddressSanitizer e
UndefinedBehaviorSanitizer, seguidos de VMs QEMU em BIOS e UEFI. Cria seus
proprios arquivos novos em /tmp; nao recebe um caminho de disco do usuario.
Uma imagem esparsa de aproximadamente 128 GiB permite exercitar LBAs acima
de 2^28 sem reservar esse volume de dados. Os arquivos temporarios sao
removidos ao terminar, inclusive quando um teste falha.

Os cenarios verificam tres discos simultaneos, master/slave, canais primario
e secundario, ATAPI ignorado, boot sem disco IDE, leitura de padroes semeados
pelo host, escrita de varios setores e do ultimo setor, flush e persistencia
em uma segunda VM. O host compara os dados e todas as extensoes alocadas do
arquivo, incluindo setores de guarda, para detectar escrita fora das regioes
previstas. O autoteste destrutivo exige uma assinatura especifica no setor
zero de TODOS os discos antes da primeira escrita e recusa discos sem ela.
Essa assinatura e uma protecao adicional para os testes, nao uma permissao
de escrita exigida pela API normal do driver.

As falhas de leitura/escrita/flush sao injetadas com o
[blkdebug do QEMU](https://www.qemu.org/docs/master/devel/testing/blkdebug.html).
O kernel deve informar status/erro ATA, recusar operacoes seguintes no canal
e continuar ate o terminal e o desligamento ACPI. Timeouts, relogio parado,
wrap do contador de ticks, erro parcial, LBA28 e setores de 4 KiB tambem sao
verificados pelo emulador de portas dos testes unitarios. O QEMU IDE usado
nos testes de integracao apresenta setores logicos de 512 bytes.

Resultados e logs: `build/storage-results.json`, `build/storage-*.log`.
Somente testes unitarios: `make check-block-unit check-ata-unit`.

Para ver um disco em uma VM interativa, crie um arquivo NOVO uma unica vez:

```sh
make iso
python3 -c 'from pathlib import Path; f=Path("build/ata-demo.raw").open("xb"); f.truncate(16*1024*1024); f.close()'
qemu-system-x86_64 -machine q35 -accel tcg -m 256M -boot d -net none \
  -cdrom build/nexusos.iso -serial mon:stdio \
  -device piix3-ide,id=storageide \
  -drive file=build/ata-demo.raw,if=none,id=disk,format=raw \
  -device ide-hd,drive=disk,bus=storageide.0,unit=0
```

No terminal do NexusOS: `disks`. Esse comando apenas consulta o catalogo;
nao grava setores. `make run` usa q35/AHCI por padrao, sem disco IDE, e deve
mostrar zero discos ATA sem impedir o boot.

## Contratos da API

`block_read(device, lba, count, buffer, bytes)` e `block_write(...)` recebem
LBA em setores logicos e tamanho do buffer em bytes. Setores suportados:
512, 1024, 2048 e 4096 bytes; no maximo 128 setores por chamada. A geometria
e validada, assim como count, buffer, aritmetica e intervalo completo de LBA.
Uma operacao vazia aceita `lba == capacidade` e nao toca no hardware.
O chamador do kernel deve fornecer memoria mapeada, com as permissoes
adequadas, durante toda a operacao. Nao existem syscalls de disco nesta etapa.

O resultado contem erro, quantidade de setores transferidos e registradores
de status/erro do dispositivo. Uma escrita parcial NAO e atomica e nao tem
rollback. Mesmo com `completed == count`, um erro no status final significa
que a operacao falhou. O driver nao repete escritas automaticamente.

`block_flush(device)` aguarda FLUSH CACHE EXT (EA) ou FLUSH CACHE (E7), conforme
as capacidades anunciadas. Dispositivo que nao anuncia flush retorna
`BLOCK_UNSUPPORTED`; escrita nao implica flush automatico. A garantia depende
tambem do dispositivo/backend respeitar o comando.

## Hardware e concorrencia

PCI configuration mechanism 1 identifica classe 01/subclasse 01. Canais IDE
em modo de compatibilidade usam 1F0/3F6 e 170/376; em modo nativo usam BARs
de I/O validos. Apenas o bit de habilitacao de I/O do PCI Command e ativado;
DMA/bus mastering nao sao usados. O driver nao escreve bits W1C de PCI Status.
AHCI, NVMe, VirtIO, dispositivos ATAPI e BARs MMIO sao informados/ignorados.
Limites atuais: quatro canais, oito discos, sem hotplug.

PIO e sincrono e exige contexto de tarefa com interrupcoes habilitadas e PIT
funcionando. Em um unico CPU, a preempcao fica suspensa durante a transacao;
as IRQs permanecem habilitadas e os ticks continuam avancando. Nao chamar em
IRQ/NMI. O driver usa nIEN no ATA e nao habilita IRQ14/15. Durante uma espera
de hardware, HLT evita polling continuo. O timeout e compartilhado pela
transacao: 1 segundo em IDENTIFY, 3 segundos em dados e 5 segundos em flush,
alem de um limite de iteracoes. Logo, uma operacao pode adiar o escalonamento
ate seu timeout. Operacoes assincronas, SMP e DMA ficam para outra etapa.

Erro de I/O ou timeout isola o canal inteiro ate reiniciar o kernel, impedindo
que dados remanescentes de uma transacao incompleta contaminem a seguinte.
O estado nao e silenciosamente zerado ou recuperado com uma nova escrita.
Ausencia de dispositivo e ATAPI nao tornam um disco saudavel do mesmo canal
indisponivel. IDENTIFY invalido e rejeitado sem registrar uma geometria falsa.

Referencia de protocolo: [T13 ATA/ATAPI Command Set](https://read.seas.harvard.edu/cs161/2019/pdf/ata-atapi-8.pdf),
especialmente IDENTIFY DEVICE, READ/WRITE SECTOR(S) EXT e FLUSH CACHE.
Descoberta do PIIX conferida com o [codigo oficial do QEMU](https://github.com/qemu/qemu/blob/v8.2.2/hw/ide/piix.c).

Esta etapa fornece acesso a blocos. Particoes, sistema de arquivos e programas
carregados do disco ainda nao fazem parte dela.
