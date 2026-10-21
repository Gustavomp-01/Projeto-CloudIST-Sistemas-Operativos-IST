#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <string.h>

#include "parser.h"
#include "datacenter.h"
#include "constants.h"
#include "filesystem.h"

// Corre os comandos de um ficheiroc (fd) até chegar ao fim (EOC)
static void processar_comandos(DataCenter *dc, int fd){
	while(1){
		switch (get_next_command(fd)){
			case CMD_DEFINE: {
				VMType vmtype;
	
				if (parse_define(fd, &vmtype) != 0) {
					fprintf(stderr, "Invalid define command. See H (help) for usage.\n");
					continue;
				}
	
				if(datacenter_define_VM(dc, &vmtype) != 0){
					fprintf(stderr, "Failed to define VM.\n");
					continue;
				}
	
				printf("VM successfully defined!\n");
	
				break;
			}
	
			case CMD_RESERVE: {
				Reservation reservation = {0};
	
				size_t num_items = parse_reserve(fd, &reservation, MAX_RESERVATIONS_ITEMS);
	
				if (num_items == 0) {
					fprintf(stderr, "Invalid reserve command. See H (help) for usage.\n");
					continue;
				}
	
				if (datacenter_reserve(dc, &reservation) != 0) {
					fprintf(stderr, "Failed to reserve VMs.\n");
					continue;
				}
	
				printf("Reservation made successfully!\n");
	
				break;
			}
	
			case CMD_EXECUTE:
				char id[MAX_STRING_SIZE];
	
				if(parse_execute(fd, id) != 0){
					fprintf(stderr, "Invalid execute command. See H (help) for usage.\n");
					continue;
				}
	
				if (datacenter_execute(dc, id) != 0) {
					fprintf(stderr, "Failed to execute reservation.\n");
					continue;
				}
	
				printf("Finished reservation execution!\n");
	
				break;
	
			case CMD_LIST:
				if (datacenter_list(dc) != 0) {
					fprintf(stderr, "Failed to list VMs.\n");
					continue;
				}
	
				break;
	
			case CMD_WAIT:
				unsigned int delay;
	
				if(parse_wait(fd, &delay) != 0){
					fprintf(stderr, "Invalid wait command. See H (help) for usage.\n");
					continue;
				}
	
				datacenter_wait(delay);
				break;
	
			case CMD_INVALID:
				fprintf(stderr, "Invalid Command. See H (help) for usage.\n");
				break;
	
			case CMD_HELP:
				printf(
					"Spaces between arguments are allowed, but not after command end.\n"
					"Available commands:\n"
					" D <VM_TYPE_ID> <INPUT_FOLDER> <EXECUTABLE_PATH> <RAM_NEEDED> <DISK_NEEDED> <VCPU_NEEDED_COUNT>\n"
					" R <RESERVATION_ID> [<VM_TYPE_ID> <COUNT> <SERVER_ID>]+\n"
					" A <RESERVATION_ID>\n"
					" L\n"
					" E <DELAY_MS>\n"
					" H\n"
				);
				break;
	
			case CMD_EMPTY:
				break;
	
			case EOC: // Endo of comands
				return;
		}
	}

}


int main(int argc, char **argv){
	DataCenter dc;
	datacenter_init(&dc);
	// Alterando argc para que ele verifique se foram recebidos 5 argumentos, por isso se diferente de 6.
	if (argc != 6) {
    fprintf(stderr, "Usage: %s <servers> <ram> <disk> <cpus> <input_dir>\n", argv[0]); // Incluimos o <input_dir> no caso de erro.
    return 1;
  }

	size_t servers;
	size_t ram;
	size_t disk;	
	double cpu;
	const char *input_dir = argv[5]; // Aponta para o valor inserido em <input_dir>

  	// Cada parse devolve 0 se a conversão correu bem e 1 se falhou
	// O parse converte texto em número.
	// Por esse fato o input_dir que já é texto não entra aqui.
	if (parse_size_t_arg(argv[1], &servers) != 0 ||
			parse_size_t_arg(argv[2], &ram) != 0 ||
			parse_size_t_arg(argv[3], &disk) != 0 ||
			parse_double_arg(argv[4], &cpu) != 0){
		fprintf(stderr, "Invalid command line arguments.\n");
		return 1;
	}

	// Verificação do Input_dir
	// No caso o path_exists checa se o diretório que selecionamos realmente existe e se é uma diretória.
	// Caso exista ele retorna 1, caso não retorna 0
	// Tratamos em caso de inexistência do diretório, ou seja retorno 0
	// O if retorna 1 se entrar no erro, se não corre normalmente.
	if(path_exists(input_dir) == 0){
		fprintf(stderr, "Invalid input directory: %s\n", input_dir);
		return 1;
	}

	
	// Se dir_manipulation falhar antes de preencher a struct, ela fica assim em vez de ter lixo,
	// e conflist_free(&lista) fica segura (o for não corre e free(NULL) não faz nada).
	ConfList lista = {0}; // {0} põe todos os campos a zero: nomes = NULL e contador = 0.
	

	// Chamamos a função de manipulação de diretórios e se o retorno dela for diferente de 0 que é = erro tratamos o erro.
	// Importante lembrar que a função vai ter que correr por que chamamos no if, a partir do retorno dela ela entra ou não na condição.
	if(dir_manipulation(input_dir, &lista)!= 0){
		fprintf(stderr, "Failed to process input repository! \n");
		return 1; // Como todo erro retornamos 1.
	}

	if(lista.contador == 0){
		fprintf(stderr, "No .conf files found in %s\n", input_dir);
		conflist_free(&lista);
		return 1;
	}
	

	Resources resources = {
    .ram = ram,
    .disk = disk,
    .cpu = cpu
	};

	if(datacenter_configure(&dc, servers, &resources) != 0){
		fprintf(stderr, "Failed to configure Data Center.\n");
		conflist_free(&lista);
		datacenter_destroy(&dc);
		return 1;
	}

	// Ciclo para a leitura de cada comando 
	for(size_t i = 0; i <lista.contador; i++){
		char caminho[MAX_PATH_SIZE];
		snprintf(caminho, sizeof(caminho), "%s/%s", input_dir, lista.nomes[i]);

		int fd = open(caminho,O_RDONLY);

		if(fd == -1){ // Em caso de o fd devolver um endereçon que não existe
			fprintf(stderr, "Cannot open %s: %s\n", caminho, strerror(errno));
			conflist_free(&lista);
        	datacenter_destroy(&dc);
        	return 1;
		}

		processar_comandos(&dc, fd);

		close(fd);

	}

	conflist_free(&lista);
	datacenter_destroy(&dc);
	return 0;

}

