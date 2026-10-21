#define _XOPEN_SOURCE 700

#include "filesystem.h"
#include "constants.h"

#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>
#include <dirent.h>
#include <errno.h>


int path_exists(const char *path){
  struct stat st;

  if (stat(path, &st) != 0)
    return 0;

  return S_ISDIR(st.st_mode);
}

int file_exists(const char *path){ // Recebe um caminho (Sómente leitura) e devolve um int que é booleano, 0 é falso e diferente de 0 é verdadeiro
  //stat (SystemCall) vai fornecer informações sobre o arquivo apontado pelo ponteiro do do caminho (*path)
  struct stat st; // Reserva uma caixa vazia na stack . O kernel vai preencher essa caixa com metadados do ficheiro (tipo, tamanho, permissões, datas)

  // Em caso de sucesso ele devolve 0 e em caso de erro -1
  if (stat(path, &st) != 0) // &st devolve o endereço da caixa para o stat escrever lá dentro.
    return 0; // Nesse caso para file_exists o retorno 0 significa que esse ficheiro não é um ficheiro regular.

  return S_ISREG(st.st_mode);
}

int absolute_path(const char *path, char *buffer, size_t size){
  char *resolved = realpath(path, NULL);

  if (resolved == NULL)
    return 1;

  if (strlen(resolved) >= size) {
    free(resolved);
    return 1;
  }

  strcpy(buffer, resolved);

  free(resolved);
  return 0;
}

// função de comparação
static int comparar_nomes(const void *a, const void *b){ // duas variáveis a serem preenchidas pelo qsort()
      const char *nome_a = *(const char * const *)a; // criamos a const nome_a que vai ser guardado nela o endereço da primeira letra do nome_a na lista
      const char *nome_b = *(const char * const *)b; // criamos a const nome_b que vai ser guardado nela o endereço da primeira letra do nome_b na lista

      return strcmp(nome_a,nome_b); // Se o primeiro nome é igual ao segundo ele devolve 0, se maior um número maior que 0 e se menor um número menor que 0
}


// Função auxiliar para a libertação free() em caso de erro
static void liberta_listas(char **lista, size_t n){
  for(size_t i = 0; i < n; i++){
    free(lista[i]);
  }
  free(lista);
}


int dir_manipulation(const char *path, ConfList *lista){
    // O DIR representa uma diretoria aberta, vamos guardar o endereço do objeto DIR (dessa diretoria) com o ponteiro *dir (Uma variável que guarda um endereço de memória)
    DIR *dir = opendir(path); // Chamada da função opendir que recebe o caminho (path) para abrir o diretório.
    char **lista_ficheiros = NULL;
    size_t nomes_copiados = 0; // Quantos nomes já temos guardados na lista
    size_t capacidade = 0; // Capacidade da lista
    // Caso o ficheiro não seja aberto o opendir deve devolver NULL, assim tratamos o erro.
    if(dir == NULL){
      fprintf(stderr, "Cannot open '%s': %s\n", path, strerror(errno));
      return 1;
    }

    struct dirent *entry; // Variável que vai receber cada entrada

    errno = 0; // Precisamos setar o errno a zero para comparar ao fim da syscall readdir

    // Atribui a entrada seguinte da diretória que dir representa. (dir) -> apenas fala para a syscall qual diretoria ler.
    // A syscall readdir() retorna NULL se ela chegar ao fim de toda a lista de ficheiros naquele diretório, ou se algum erro ocorrer.
    while((entry = readdir(dir)) != NULL){

      // Devolve um ponteiro para a o ultimo '.' da string.
      const char *ponteiro_devolvido = strrchr(entry->d_name, '.');

      if(ponteiro_devolvido != NULL){

          int string_comp = strcmp(ponteiro_devolvido ,".conf");

          if (string_comp == 0){

            char path_destined [MAX_PATH_SIZE];

            snprintf(path_destined, sizeof(path_destined), "%s/%s", path, entry->d_name);

            if(file_exists(path_destined) != 0){
               
              char *copia = strdup(entry->d_name);

               if(copia == NULL){
                 fprintf(stderr, "Cannot copy '%s': %s\n", path, strerror(errno));
                 liberta_listas(lista_ficheiros, nomes_copiados);
                 closedir(dir);
                 return 1;
               }

               size_t nova_capacidade = 0;

               if(nomes_copiados == capacidade){ // Se a lista tiver cheia
                
                if(capacidade == 0){
                  nova_capacidade = 1;
                }else if (capacidade > 0){
                  nova_capacidade = capacidade * 2;
                }

                // aqui criamos uma nova lista por segurança e reservamos nela o espaço para os ponteiros que vão vir
                char **nova_lista_ficheiros = realloc(lista_ficheiros, nova_capacidade * sizeof(char *));

                if(nova_lista_ficheiros == NULL){ // em caso de falha do realloc

                  free(copia); // Liberta a copia do nome atual se o realloc falhar
                  
                  liberta_listas(lista_ficheiros, nomes_copiados);
              
                  closedir(dir);

                  return 1; // Quando há algum erro na função dir_manipulation
                }

                // Agora caso o realloc corra bem
                lista_ficheiros = nova_lista_ficheiros;
                capacidade = nova_capacidade;
              }
              
              lista_ficheiros[nomes_copiados] = copia; // Preenchendo a lista (copia) -> guarda o nome a ser passado para a lista e nomes copiados a posição livre no array
              nomes_copiados ++; // Incrementa o número de nomes copiados

               
            }
          
          
        } 

      }

      errno = 0; // Em caso de link partido o file exists vai alterar o errno mas vai continuar o código

    }


    // Se ocorrer algum erro na leitura, errno vai ser NULL ou seja, diferente de 0
    if(errno != 0){
      fprintf(stderr, "Error reading '%s': %s\n", path, strerror(errno));
      liberta_listas(lista_ficheiros, nomes_copiados);
      closedir(dir); // Fecha o diretório
      return 1; // Em caso de erro, no main estamos retornando sempre 1
    }

    // Agora que já temos a lista vamos fazer a ordenação alfabética dela

    if(nomes_copiados != 0){
      // 1 -> lista que queremos ordenar 2 -> Número de elementos 3 -> Tamanho de um elemento (São ponteiros 8 bytes) 4 -> chama a função comparar
      qsort(lista_ficheiros, nomes_copiados, sizeof(char *), comparar_nomes); // A partir do resultado de comparar qsort() ordena a lista
    }


    // Preenche a struct lista 
    lista->nomes = lista_ficheiros;
    lista->contador = nomes_copiados;
    

    closedir(dir); // Fecha o diretório
    return 0; // Em caso de sucesso, no main estamos retornando sempre 0


}


void conflist_free(ConfList *lista){
  
  if(lista == NULL){ // Se a Struct nem tiver sido passada no main
      return; // Não há o que apagar, por isso o return
  }

  for(size_t i = 0; i < lista->contador; i++){ // Percorre a lista toda
      free(lista->nomes[i]); //Liberta o nome que está dentro desse ponteiro
  }
  free(lista->nomes); // Liberta toda a lista incluindo os ponteiros que agora já não apontam para mais nada
  lista->nomes = NULL;
  lista->contador = 0;
}

// Função copy file
int copy_file(const char *origem, const char *destino){ // A função receberá dois ponteiros que apontam para o texto com o caminho do ficheiro origem e do ficheiro destino
  int fd_origem = open(origem, O_RDONLY);

  if(fd_origem == -1){ // Se o file descriptor não for um número válido, ou seja, erro ao encntrálo atribuindo -1 retornamos
    return 1; // erro
  }

  int fd_destino = open(destino, O_WRONLY | O_CREAT | O_TRUNC, 0644); // O 0644 é para o modo e permissões do O_CREAT o Dono pode ler e escrever o grupo e outros só ler

  if(fd_destino == -1){ // Se o file descriptor não for um número válido, ou seja, erro ao encntrálo atribuindo -1 retornamos
    close(fd_origem); // Fechamos o fd_origem que estava aberto
    return 1; // Erro
  }

  char buffer[4096]; // buffer para o read, deve ser esse valor por que a cada read ele utilizará um buffer de 4096 (Ou seja, a cada read conseguimos ler 4096 bytes)
  ssize_t lidos; // Aqui usamos o ssize_t e não size_t pois o read pode devolver negativo em caso de erro e o size_t só recebe números inteiros positivos já o ssize_t signed ones

  // Começa a leitura do ficheiro
  // Lembrando que o read retorna 0 quando ler todo o ficheiro | -1 quando há erro | n > 0 para quando tiver lido um valor n mas não todo o ficheiro
  while((lidos = read(fd_origem, buffer, sizeof(buffer))) > 0){ // Read( 1 -> O ficheiro de onde ler, 2 -> Onde guarda os bytes lidos, 3 -> O máximo de bytes que aceita ler de uma vez)

    ssize_t escritos = 0;

    while(escritos < lidos){
      ssize_t n = write(fd_destino, buffer + escritos, (size_t)(lidos-escritos)); // Write ( 1 -> ficheiro onde escrever, 2 -> Endereço de onde tirar os os bytes da origem para escrever no destino, 3 -> o tanto que falta escrever)
      
      if(n == -1){ // Em caso de erro no write ele vai retornar -1
        close(fd_destino);
        close(fd_origem);
        return 1;
      }
      escritos += n;
    }
  }


// Em caso de erro do read
if(lidos < 0){
  close(fd_origem);
  close(fd_destino);
  return 1;
  }

  // Em caso de sucesso
close(fd_destino);
close(fd_origem);
return 0;

}

// Função que cria uma pasta, se já existir uma não é erro !
// Como a função é static e só vamos usar aqui no filesystems.c não precisamos prototipar ela no Header.
static int criar_dir(const char *caminho){

  if(mkdir(caminho, 0755) == -1 && errno != EEXIST){ // Em caso de falha na criação do dir o mkdir devolve -1 e se o errno for diferente de EEXIST (Path already exist) quer dizer que falhou por outra coisa
      return 1; // Falhou por outra razão
  }

  // Em caso de falha por que o dir já existe ele segue e dá como sucesso.
  return 0;
}

int criar_diretorios_por_reserva(const char *reserva_id, const char *vm_id, char *destino, size_t tamanho){ // (1 -> Recebe o ponteiro para o id da reserva | 2 -> Ponteiro para o ID da vm)
                                                                                                                  // (3-> ponteiro para o diretório destino | 4-> Tamanho do buffer destino em bytes)
  char nivel2[MAX_PATH_SIZE];

  snprintf(nivel2,sizeof(nivel2), "/tmp/CloudIST/%s", reserva_id); // Segundo, /tmp/CloudIST/<reserva_id> (TEXTO DA PASTA)
  snprintf(destino,tamanho, "%s/%s", nivel2, vm_id); // Terceiro, /tmp/CloudIST/<reserva_id>/<vm_id> (TEXTO DA PASTA)

  // Aqui ele só vai dar um se o dir não conseguir ser criado por algum outro motivo (Se ele já existir ele não entra nesse IF)
  if(criar_dir("/tmp/CloudIST") != 0 || criar_dir(nivel2) != 0 || criar_dir(destino) != 0){ // Se algum falhar ele entra no if
    return 1; // Em caso de erro
  }

  return 0; // Em caso de sucesso.

}

// O que essa função faz é: 
// Copiar input/config.txt para dentro do destino, ficando /tmp/CloudIST/res_maria/res_maria_1/config.txt. 
// O nome do ficheiro mantém-se, e muda a pasta onde ele fica.
// Copia um diretório inteiro recursivamente, inclusive suas subpastas
int copy_dir(const char *origem, const char *destino){ // Em outras palavras a função recebe uma pasta de origem e copia o conteúdo para a pasta de destino
    DIR *dir = opendir(origem); // Abre o diretório origem

    if(dir == NULL){ // Em caso de erro
      return 1;
    }

    struct dirent *entry; // Variável que vai receber cada entrada

    errno = 0; // Precisamos setar ele a 0 para que possamos verificar após o readdir

    // Readdir vai descobrir o que existe no diretório e atribuir para d_name a cada volta 
    while((entry = readdir(dir)) != NULL){ // Readdir devolve null quando chega ao fim ou quando um erro ocorre, logo precisamos setar o errno antes e testar ao fim
      
      if(strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0){ // Fazemos  a verificação para ignorar os "." e ".." que as pastas contem.
        continue;
      }

      char caminho_origem[MAX_PATH_SIZE];
      char caminho_destino[MAX_PATH_SIZE];
      
      // snprintf(Escreve dentro de um buffer que indicamos variáveis que já existem)
      int n = snprintf(caminho_origem, sizeof(caminho_origem), "%s/%s", origem, entry->d_name);
      //               onde escrever   tamanho do buffer        molde   1º  %s   2º %s
      if(n < 0 || (size_t)n >= sizeof(caminho_origem)){ // Em caso de o caminho for maior que o MAX_PATH_SIZE ou menor que 0
        closedir(dir);
        return 1;
      }

      // snprintf(Escreve dentro de um buffer que indicamos variáveis que já existem)
      int x = snprintf(caminho_destino, sizeof(caminho_destino), "%s/%s", destino, entry->d_name);
      //                 onde escrever   tamanho do buffer        molde   1º  %s   2º %s
      if(x < 0 || (size_t)x >= sizeof(caminho_destino)){ // Em caso de o caminho for maior que o MAX_PATH_SIZE ou menor que 0
        closedir(dir);
        return 1;
      }

      struct stat st; // Caixa vazia na stack

      if(stat(caminho_origem, &st) != 0){ // Stat(1 -> Pega os dados de caminho_origem | 2 -> Põe no endereço de st preenche a caixa criada na stack retorna 0 em sucesso em erro -1 é retornado
        closedir(dir); // Fechamos o diretório que estava aberto em caso de erro
        return 1;
      }


      //st_mode guarda as informações do ficheiro, incluido se é um diretório ou um file
      if(S_ISDIR(st.st_mode)){ // Se o ficheiro for um diretório

        int r1 = criar_dir(caminho_destino); // Criar o Diretório destino

        if(r1 == 1){ // Em caso de erro no criardir
          closedir(dir); // Fecha a diretória
          return 1;
        }

        //Chamada recursiva abaixo
        int r2 = copy_dir(caminho_origem,caminho_destino); // Copiar o conteúdo do diretório da origem para o destino

        if(r2 == 1){ // Em caso de erro no copydir
          closedir(dir); // Fecha a diretória
          return 1;
        }
        
        
      }else if(S_ISREG(st.st_mode)){ // Se o ficheiro for um regular file
        int r3 = copy_file(caminho_origem, caminho_destino);

        if(r3 == 1){ // Se der algum erro na copia do ficheiro
          closedir(dir);
          return 1;
        }
        
      }

      errno = 0; // Setamos o errno a 0 novamente a cada ciclo do while

    }
    

    // Em caso de erro do readdir
    if(errno != 0){

      closedir(dir);
      return 1; 
    }

    // Em caso de sucesso
    closedir(dir);
    return 0;    
}