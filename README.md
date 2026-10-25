# cloudIST

This repository contains implementation for the cloudIST operating systems project.
The program simulates a data center that manages servers, VM types, reservations, and VM execution commands.

## Project Structure

### Core source files

- `main.c`: Program entry point, command loop, and command-line configuration.
- `parser.c` / `parser.h`: Parsing of command-line values and commands read from
  standard input.
- `datacenter.c` / `datacenter.h`: Data center configuration, VM type
  registration, reservations, execution, listing, and waits.
- `datacenter_utils.c` / `datacenter_utils.h`: VM type and reservation lookup,
  reservation validation and allocation, cleanup, and VM lifecycle helpers.
- `resources.c` / `resources.h`: Resource operations for CPU, RAM, and disk.
- `filesystem.c` / `filesystem.h`: Filesystem checks and absolute-path
  resolution.
- `constants.h`: Limits used by the data center, reservations, VM types, and
  identifiers.

---

## Como compilar e arrancar (guia rápido)

### Compilar e limpar

```
make          # compila e gera o executável ./cloudIST (as flags tratam avisos como erros)
make clean    # apaga o executável e os ficheiros .o
```

### Requisitos

- **Linux/POSIX** (usa `fork`, `exec`, `setrlimit`, `waitpid`).
- **`cpulimit` com as opções `-q` e `-f`** (versão 3.x). Confirmar com `cpulimit -h`.
  No Debian/Ubuntu (e no cluster): `sudo apt install cpulimit`.
  No EndeavourOS o pacote `cpulimit` do AUR é a versão antiga (0.2, sem `-q`/`-f`);
  o `yay -S limitcpu` instala a versão certa com o nome `limitcpu`, por isso é preciso
  um atalho `~/bin/cpulimit` que faça `exec limitcpu "$@"`.

### Sintaxe

```
./cloudIST <SERVERS> <RAM> <DISK> <CPU> <LOCAL_CORES> <INPUT_DIR>
```

Exercício 3 (quando estiver implementado) acrescenta um argumento no fim:

```
./cloudIST <SERVERS> <RAM> <DISK> <CPU> <LOCAL_CORES> <INPUT_DIR> <MAX_THREADS>
```

| Posição | Argumento | O que é | Exemplo |
|---|---|---|---|
| `argv[1]` | `SERVERS` | Número de servidores (máquinas físicas) do data center | `2` |
| `argv[2]` | `RAM` | RAM de **cada** servidor, em **GB** | `8` |
| `argv[3]` | `DISK` | Disco de **cada** servidor, em **GB** | `100` |
| `argv[4]` | `CPU` | CPUs (cores) de **cada** servidor | `8` |
| `argv[5]` | `LOCAL_CORES` | Cores **reais** do computador onde o programa corre (usado só no cálculo do `cpulimit`; tem de ser > 0) | `4` |
| `argv[6]` | `INPUT_DIR` | **Pasta** com os ficheiros `.conf` (scripts de comandos), não o caminho de um ficheiro | `~/teste_conf` |
| `argv[7]` | `MAX_THREADS` | (Ex.3) Número máximo de worker threads que processam `.conf` em paralelo | `4` |

Exemplo:

```
./cloudIST 2 8 100 8 4 ~/teste_conf
```

Significa: 2 servidores, cada um com 8 GB de RAM, 100 GB de disco e 8 CPUs; o
computador local tem 4 cores; os comandos estão nos `.conf` de `~/teste_conf`.

Em caso de argumentos errados, o programa escreve a usage e sai com código `1`.

### Como o programa lê o `INPUT_DIR`

- Só considera ficheiros **regulares** cujo nome termina em `.conf` (subpastas e outros ficheiros são ignorados).
- Processa-os por **ordem alfabética** (comparação com `strcmp`): `1.conf`, `10.conf`, `2.conf`, `a.conf`, `b.conf`.
- Se a pasta não existir ou não tiver nenhum `.conf`, escreve um erro e sai com código `1`.
- O data center é criado **uma vez**: reservas e tipos de VM persistem de um `.conf` para o seguinte.
- Linhas em branco e linhas que começam por `#` são ignoradas.

### Comandos de um ficheiro `.conf`

| Comando | Sintaxe | O que faz |
|---|---|---|
| `D` | `D <VM_TYPE_ID> <INPUT_FOLDER> <EXECUTABLE_PATH> <RAM_NEEDED> <DISK_NEEDED> <VCPU_NEEDED_COUNT>` | Define um tipo de VM. A pasta de input e o executável têm de existir. RAM e disco em GB. |
| `R` | `R <RESERVATION_ID> [<VM_TYPE_ID> <COUNT> <SERVER_ID>]+` | Reserva VMs (`COUNT` do tipo, no servidor `SERVER_ID`). Só valida se os recursos do servidor chegarem. |
| `A` | `A <RESERVATION_ID>` | Executa a reserva: copia o input, lança um processo por VM e espera que terminem. |
| `L` | `L` | Lista servidores, VMs e reservas. |
| `E` | `E <DELAY_MS>` | Espera `DELAY_MS` milissegundos. |
| `H` | `H` | Mostra a ajuda. |

Exemplo de `.conf`:

```
# define o tipo V1: input, executável, 2 GB RAM, 50 GB disco, 4 vCPUs
D V1 /home/gustavo17/teste_destino /bin/true 2 50 4

# reserva res_teste: 2 VMs do tipo V1 no servidor 1
R res_teste V1 2 1

# executa a reserva
A res_teste
```

Cuidado nas contas: as 2 VMs de `V1` pedem `2 x 4 = 8` CPUs, e o servidor 1 só tem os `CPU` do arranque.

### O que acontece quando se executa uma reserva (`A`)

Para cada VM da reserva, o processo principal (pai):

1. cria `/tmp/CloudIST/<ID-reserva>/<ID-VM>` (ex.: `/tmp/CloudIST/res_teste/res_teste_1`);
2. copia recursivamente a pasta de input do tipo de VM para lá;
3. faz `fork`; o filho aplica `setrlimit` (RAM e disco) e faz `exec` de
   `cpulimit -q -f -l <CPULIMIT> -- <EXECUTABLE_PATH>`;
4. guarda o PID do filho e passa a VM a `RUNNING`.

Depois espera por cada filho (`waitpid`) e passa a VM a `TERMINATED`.

Limites aplicados a cada VM:

| Recurso | Mecanismo | Valor |
|---|---|---|
| RAM | `setrlimit(RLIMIT_AS)` | `RAM da VM (GB) << 30` bytes |
| Disco | `setrlimit(RLIMIT_FSIZE)` | `disco da VM (GB) << 30` bytes (máximo por ficheiro) |
| CPU | `cpulimit -l` | ver fórmula abaixo |

Fórmula do CPULIMIT (percentagem de **um** core; 100 = 1 core):

```
CPULIMIT = 100 x vCPUs_da_VM x LOCAL_CORES / (CPU_por_servidor x SERVERS)
```

Exemplo: `./cloudIST 2 8 100 8 4 ...` e uma VM de 4 vCPUs: `100 x 4 x 4 / (8 x 2) = 100`.
Máximo possível: `100 x LOCAL_CORES`.

### Atalhos para testar

```
rm -r /tmp/CloudIST                       # limpar as cópias antigas
ls -R /tmp/CloudIST                       # ver as pastas criadas
diff -r <pasta_de_input> /tmp/CloudIST/<reserva>/<vm>   # vazio = cópia idêntica
strace -e trace=openat ./cloudIST ... 2>&1 | grep conf  # ordem de abertura dos .conf
echo $status                              # (fish) código de saída do último comando
```

### Códigos de saída

- `0`: tudo correu bem.
- `1`: erro (argumentos inválidos, pasta inexistente ou sem `.conf`, falha ao configurar, etc.).