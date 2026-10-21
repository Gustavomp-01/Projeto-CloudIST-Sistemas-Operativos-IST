#ifndef FILESYSTEM__H
#define FILESYSTEM__H

#include <stddef.h>

/**
 * Checks whether a path exists and is a directory.
 *
 * @param path Directory path.
 *
 * @return 1 if it exists and is a directory, 0 otherwise.
 */
int path_exists(const char *path);

/**
 * Checks whether a path exists and is a regular file.
 *
 * @param path File path.
 *
 * @return 1 if it exists and is a regular file, 0 otherwise.
 */
int file_exists(const char *path);

/**
 * Converts the given path into an absolute path, resolving symbolic links,
 * relative components ('.' and '..'), and redundant separators. The resolved
 * path is copied into the provided buffer.
 *
 * @param path Path to resolve.
 * @param buffer Destination buffer where the absolute path will be stored.
 * @param size Size of the destination buffer, in bytes.
 *
 * @return 0 if the path was successfully resolved and copied to the buffer
 * @return 1 if the path could not be resolved or the buffer is too small.
 */
int absolute_path(const char *path, char *buffer, size_t size);



typedef struct ConfList { // Quem vai preencher essa lista é o dir_manipulation
    char **nomes; // Uma lista de ponteiros que aponta para os nomes
    size_t contador; 
} ConfList; // Isso é para podermos chamar apenas ConfList no main sem ter que escrever struct

/**
 * Abre um diretório e lê os ficheiros e diretórios dentro dele, recebe um *path caminho da diretoria a abrir
 * @param path endereço do diretório indicado a ser aberto e lido
 * @return 0 em caso de sucesso
 * @return 1 em caso de erro
 */
int dir_manipulation(const char *path, ConfList *lista);

void conflist_free(ConfList *lista);

int copy_file(const char *origem, const char *destino);


int criar_diretorios_por_reserva(const char *reserva_id, const char *vm_id, char *destino, size_t tamanho);

int copy_dir(const char *origem, const char *destino);

#endif // FILESYSTEM__H