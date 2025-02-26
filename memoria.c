#define _GNU_SOURCE // Deve vir antes de qualquer include
#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <sys/types.h>
#include <fcntl.h>
#include <unistd.h> // Adicionado para munmap()

#define HUGE_PAGE_SIZE (2 * 1024 * 1024)

void *alocar_huge_page()
{
    void *page = mmap(NULL, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

    if (page == MAP_FAILED)
    {
        perror("\nErro na alocação de Huge Page");
        fprintf(stderr, "Verifique:\n"
                        "1. sudo sysctl vm.nr_hugepages=2\n"
                        "2. Permissões de usuário\n"
                        "3. Memória disponível\n");
        return NULL;
    }
    return page;
}

void liberar_huge_page(void *page)
{
    if (page != NULL)
    {
        munmap(page, HUGE_PAGE_SIZE);
    }
}

void gerenciamento_memoria()
{
    void *page = alocar_huge_page();
    if (page)
    {
        printf("Huge Page alocada em %p\n", page);
        liberar_huge_page(page);
    }
}