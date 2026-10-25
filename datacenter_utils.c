#include "datacenter_utils.h"
#include "filesystem.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/wait.h>
#include <sys/resource.h>
#include <errno.h>

VMType *VMType_exists(DataCenter *dc, const char* type_id){
	for(size_t i = 0; i < dc->num_vm_types; i++){
		if(strncmp(type_id, dc->vm_types[i].id, MAX_STRING_SIZE) == 0){
			return &dc->vm_types[i];
		}
	}
	return NULL;
}

/**
 * Checks if a reservation with the given id already exists in the Data Center.
 * 
 * @param dc Pointer to Data Center.
 * @param reservation_id Id of the reservation being checked.
 * 
 * @return 1 if the reservation exists.
 * @return 0 if the reservation does not exist.
 */	
static int reservation_exists(DataCenter *dc, const char* reservation_id){
	for(size_t i = 0; i < dc->num_reservations; i++){
		if(strncmp(reservation_id, dc->reservations[i].id, MAX_STRING_SIZE) == 0){
			return 1;
		}
	}
	return 0;
}

int reservation_validate(DataCenter *dc, Reservation *reservation) {
	if(reservation_exists(dc, reservation->id)){
		fprintf(stderr, "Reservation with id %s already exists.\n", reservation->id);
		return 1;
	}

	Resources available[dc->num_servers];
	size_t hosted_count[dc->num_servers];
	size_t vms_count = 0;

	/* Copy current server resources and hosted_vms so we can simulate the allocation. */
	for (size_t i = 0; i < dc->num_servers; i++) {
		available[i] = dc->servers[i].available;
		hosted_count[i] = dc->servers[i].num_hosted_vms;
	}

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];

		if (item->amount == 0) {
			fprintf(stderr, "Reservation item must request at least one VM.\n");
			return 1;
		}

		VMType *vm_type = VMType_exists(dc, item->vm_type_id);

		if(vm_type == NULL){
			fprintf(stderr, "VM of type %s does not exist\n", item->vm_type_id);
			return 1;
		}

		if (item->server_id == 0 || item->server_id > dc->num_servers) {
			fprintf(stderr, "Invalid server in reservation.\n");
			return 1;
		}

		for (size_t j = 0; j < item->amount; j++) {
			if(vms_count >= MAX_RESERVATION_VMS){
				fprintf(stderr, "Reached max of VMS for a reservation.\n");
				return 1;
			}
			
			if (hosted_count[item->server_id - 1] >= MAX_HOSTED_VMS) {
        fprintf(stderr,
                "Server \"%zu\" reached the maximum number of hosted VMs.\n",
                item->server_id);
        return 1;
    	}

			if (!resources_can_fit(available[item->server_id-1], vm_type->required)) {
				fprintf(stderr, "Server \"%zu\" does not have enough resources.\n", item->server_id);
				return 1;
			}

			resources_sub(&available[item->server_id-1], vm_type->required);
			hosted_count[item->server_id-1]++;
			vms_count++; // One VM "reserved"
		}

		// Attach vm_type to item for later use.
		item->vm_type = vm_type;
	}

	return 0;
}

/**
 * Rolls back the changes made by a reservation operation.
 * 
 * @param reservation Pointer to the reservation being rolled back.
 * @param initial_vms Number of VMs that existed before the operation.
 */
static void reservation_rollback(Reservation *reservation, size_t initial_vms){
	while (reservation->num_vms > initial_vms) {
		VM *vm = reservation->vms[reservation->num_vms - 1];

		Server *server = vm->server;

		// Restore resources
		resources_add(&server->available, vm->type->required);

		// Remove VM from server hosted list
		server->num_hosted_vms--;

		server->hosted_vms[server->num_hosted_vms] = NULL;

		// Remove VM from reservation
		reservation->num_vms--;

		reservation->vms[reservation->num_vms] = NULL;

		free(vm);
	}
}

int reservation_commit(DataCenter *dc, Reservation *reservation){
	// Keep track of how many vms existed before.
	size_t initial_vms = reservation->num_vms;

	for (size_t i = 0; i < reservation->items_count; i++) {
		ReservationItem *item = &reservation->items[i];
		Server *server = &dc->servers[item->server_id - 1];

		for (size_t j = 0; j < item->amount; j++) {
			VM *new_vm = calloc(1, sizeof(VM));
			if (new_vm == NULL) {
				fprintf(stderr, "Failed to allocate VM.\n");
				reservation_rollback(reservation, initial_vms);
				return 1;
			}

			snprintf(new_vm->id,
				MAX_VM_ID_STRING,
				"%s_%zu",
				reservation->id,
				reservation->num_vms + 1
			);

			new_vm->type = item->vm_type;
			new_vm->server = server;
			new_vm->state = VM_STATE_RESERVED;

			resources_sub(&server->available, item->vm_type->required);

			server->hosted_vms[server->num_hosted_vms++] = new_vm;
			reservation->vms[reservation->num_vms++] = new_vm;
		}
	}

	reservation->state = RES_STATE_PENDING;

	return 0;
}

Reservation *find_pending_reservation(DataCenter *dc, const char *reservation_id) {
	Reservation *res = NULL;

	for (size_t i = 0; i < dc->num_reservations; i++) {
		if (strcmp(dc->reservations[i].id, reservation_id) == 0) {
			res = &dc->reservations[i];
			break;
		}
	}

	if (!res) {
		fprintf(stderr, "Reservation ID %s not found.\n", reservation_id);
		return NULL;
	}

	if (res->state != RES_STATE_PENDING) {
		fprintf(stderr, "Reservation ID %s is not in a pending state.\n", reservation_id);
		return NULL;
	}

	return res;
}

// Destrói UMA VM: devolve recursos, tira-a do servidor e da reserva, liberta-a.
void vm_destroy(Reservation *reservation, VM *vm){
	Server *server = vm->server;

	// Return the VM resources back to the hosting server.
	resources_add(&server->available, vm->type->required);

	
	// Remove the VM from the server's hosted VM list.
	for (size_t j = 0; j < server->num_hosted_vms; j++) {
		if (server->hosted_vms[j] == vm) {
			// Shift the remaining VMs one position to the left.
			memmove(&server->hosted_vms[j],
						&server->hosted_vms[j + 1],
							(server->num_hosted_vms - j - 1) * sizeof(VM *));

				server->num_hosted_vms--;
				break;
			}
		}

	// Remove the VM from the reservation's VM array.
	for (size_t i = 0; i < reservation->num_vms; i++) {
		if (reservation->vms[i] == vm) {
			// Shift the remaining VMs one position to the left.
			memmove(&reservation->vms[i],
					&reservation->vms[i + 1],
					(reservation->num_vms - i - 1) * sizeof(VM *));

			reservation->num_vms--;
			reservation->vms[reservation->num_vms] = NULL;
			break;
		}
	}
	free(vm);	
}

// Tira a reserva da lista do datacenter (assume que já não tem VMs).
void reservation_remove(DataCenter *dc, Reservation *reservation){

	// Find the reservation inside the Data Center reservation list.
	size_t idx;
	for (idx = 0; idx < dc->num_reservations; idx++) {
			if (&dc->reservations[idx] == reservation)
				break;
	}

	// Reservation not found (should never happen).
	if (idx == dc->num_reservations)
		return;

	// Remove the reservation by shifting the remaining ones.
	memmove(&dc->reservations[idx],
					&dc->reservations[idx + 1],
					(dc->num_reservations - idx - 1) * sizeof(Reservation));

	dc->num_reservations--;

}


void spawn_vm_child(VM *vm, const char *limite) {
	struct rlimit lim; // Criamos uma caixa vazia chamada lim (rlimit) = É o nome do tipo de variável a ser guardado na struct.

	// Guardaremos rlim_cur e rlim_max na caixa lim
	// Limite da RAM (RLIMIT_AS) em Bytes

	// Os valores são iguais pois o processo filho não pode aumentar.
	lim.rlim_cur = vm->type->required.ram << 30;
	lim.rlim_max = vm->type->required.ram << 30;

	// RLIMIT_AS -> Tamanho máximo de uma memória virtual de um processo (Espaço de endereços)
	// Setrlimit() pega o valor limite que está em lim e seta o limite para a memória virtual a esse valor
	// Limite memória virtual = lim
	if(setrlimit(RLIMIT_AS,&lim) == -1){ // Em caso de erro ele retorna -1
		fprintf(stderr, "Fail to set virtual memory limit to the VM %s\n", vm->id);
		_exit(1); // O filho não pode terminar, então termina-se
	}

	// Limite de Disco (RLIMIT_FSIZE) em bytes
	
	// Agora vamos sobrescrever os valores da caixa lim
	// Os valores são iguais pois o processo filho não pode aumentar.
	lim.rlim_cur = vm->type->required.disk << 30;
	lim.rlim_max = vm->type->required.disk << 30;


	// RLIMIT_FSIZE -> Tamanho máximo em bytes de arquivos que um processo pode criar
	// Setrlimit() pega o valor limite que está em lim e seta o limite para o disco a esse valor
	// Limite de memória de disco = lim
	if(setrlimit(RLIMIT_FSIZE,&lim) == -1){ // Em caso de erro ele retorna -1
		fprintf(stderr, "Fail to set disk limit to the VM %s\n", vm->id);
		_exit(1); // O filho não pode terminar, então termina-se
	}



	// exec do pacote CPULIMIT
	//execlp(1º Programa a exec, 2º o argv[0] do programa novo, 3º -q (quiet) não escreve mensagens de estado
	//execlp(4º -f (Foreground) o cpulimit fica a correr a espera que o programa a vm acabe, 5º e 6º -l e limite a percentagem máxima de CPU)
	//execlp(7º "--" fim das opções do cpulimit, o que vem a seguir pertence ao programa da vm)
	//execlp(8º vm->type->exec_path o programa da vm que o cpulimit lança e limita 9º NULL marca o fim da lista de argumentos)
	execlp("cpulimit", "cpulimit", "-q", "-f", "-l", limite, "--", vm->type->exec_path, NULL);

	// Só chega aqui se o exec falhar
	perror("Exec cpulimit"); // perror escreve a mensagem inserida seguida do motivo do erro
	_exit(1); // garante que se o exec falhar o filho morre ali


	
}

int spawn_all_vms(DataCenter *dc, Reservation *res) { // Colocamos o novo parametro DC
	for (size_t i = 0; i < res->num_vms; i++) {
		VM *vm = res->vms[i]; // Array de ponteiros para a VM

		//Para cada VM da reserva o ciclo vai fazer três coisas
		// 1 -> Criar pastas /tmp/CloudIST/<reserva>/<vm> com a criar_diretorios_por_reserva. O caminho final fica guardado em destino.
		// 2 -> Copiar o Input, com a copy_dir. Copia a pasta que o comando D indicou (Input_folder do tipo de VM) para esse destino.
		// 3 -> Deixar a VM Arrancar com a linha que já está la, spawn_vm_child(vm).

		char destino[MAX_PATH_SIZE];

		if(criar_diretorios_por_reserva(res->id, vm->id, destino, sizeof(destino)) != 0){
			fprintf(stderr, "Fail to create the dir per reserve(ID): %s\n", vm->id);
			return 1;
		}

		if(copy_dir(vm->type->input_folder, destino) != 0){
			fprintf(stderr, "Fail to copy the dir per reserve(ID): %s\n", vm->id);
			return 1;
		}

		// Porcentagem de CPU desta VM(Formula)
		double limite = 100 * vm->type->required.cpu *(double)dc->local_cores / (vm->server->total.cpu *(double)dc->num_servers);
		// constante -> 100  vCPUs                              local cores       CPU por server                   num de servers

		char limite_str[32];

		
		int limite_int = (int)(limite + 0.5);
		
		if (limite_int < 1) {
			limite_int = 1;
		}
		
		snprintf(limite_str, sizeof(limite_str), "%d", limite_int);

		// O processo pai (CloudIST) continua a ler os comandos dos .conf.
		// Aqui só lança um filho por VM (fork); quem espera por elas (waitpid)
		// e destrói cada VM quando termina são as vigias, criadas no datacenter_execute.


		pid_t pid = fork(); //Fork cria um novo processo duplicando o processo pai.
		// Em caso de sucesso o Fork devolve 0 ao filho 
		// O processo pai vai receber o PID do filho, PID > 0
		// Em caso de erro ele retorna -1

		if(pid < 0){ // Em caso de erro, onde PID = -1
			fprintf(stderr, "Fork failed for VM %s\n", vm->id);
			return 1;
		}

		else if(pid == 0){ // Aqui é o código para o processo filho
			// A partir dessa parte o processo filho vai ser lançado para a sua VM
			spawn_vm_child(vm, limite_str);
			_exit(1);
		}else{ // Aqui para o processo pai
			
			vm->pid = pid; // O pai guarda o PID do filho para que depois caso necessário possa "comunicar" com o processo filho. Será usado em funções como a de wait
			vm->state = VM_STATE_RUNNING; // altera o estado da VM para running
		}


	}

	return 0;
}

// Espera que a VM com este pid termine e avisa se falhou.
void wait_for_vm(pid_t pid, const char *vm_id){
	int status;

	// Aqui esperamos que o processo termine.
		if(waitpid(pid, &status,0) == -1){ // Caso ocorra algum erro no waitpid()
			fprintf(stderr, "Waitpid failed for vm (%s) pid %d: %s\n", vm_id, (int)pid, strerror(errno));

		}else{// Caso não ocarra nenhum erro teremos certeza que o processo fiho terminou. Logo podemos mudar o estado para terminado.

			if (WIFEXITED(status)) {
    			if (WEXITSTATUS(status) != 0) {
        			fprintf(stderr, "VM %s terminou com código %d\n", vm_id, WEXITSTATUS(status));
    		}
				} else if (WIFSIGNALED(status)) {
    				fprintf(stderr, "VM %s foi terminada pelo sinal %d\n", vm_id, WTERMSIG(status));
			}
		}

}


const char *vm_state_to_string(VMState state) {
    switch (state) {
        case VM_STATE_RESERVED:   return "RESERVED";
        case VM_STATE_RUNNING:    return "RUNNING";
        case VM_STATE_TERMINATED: return "TERMINATED";
        default:                  return "UNKNOWN";
    }
}

const char *res_state_to_string(ReservationState state) {
    switch (state) {
			case RES_STATE_PENDING:  return "PENDING";
			case RES_STATE_RUNNING:  return "RUNNING";
			case RES_STATE_FINISHED: return "FINISHED";
			default:                 return "UNKNOWN";
    }
}