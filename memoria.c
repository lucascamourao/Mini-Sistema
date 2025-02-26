#include <stdio.h>
#include <stdlib.h>
#include <sys/mman.h>
#include <linux/mman.h>

#define HUGE_PAGE_SIZE (2 * 1024 * 1024) // 2 MB

// Aloca uma huge page
void *alocar_huge_page()
{
    void *page = mmap(NULL, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
                      MAP_PRIVATE | MAP_ANONYMOUS | MAP_HUGETLB, -1, 0);

    if (page == MAP_FAILED)
    {
        perror("Falha ao alocar huge page com MAP_HUGETLB");
        printf("Tentando alocar memória normal como fallback...\n");

        // Tenta alocar memória normal como fallback
        page = mmap(NULL, HUGE_PAGE_SIZE, PROT_READ | PROT_WRITE,
                    MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);

        if (page == MAP_FAILED)
        {
            perror("Falha ao alocar memória normal");
            return NULL;
        }

        printf("Memória normal alocada com sucesso (sem huge pages)\n");
    }
    else
    {
        printf("Huge page alocada com sucesso\n");
    }

    return page;
}

// Libera uma huge page
void liberar_huge_page(void *page)
{
    if (page != NULL && page != MAP_FAILED)
    {
        if (munmap(page, HUGE_PAGE_SIZE) == -1)
        {
            perror("Erro ao liberar huge page");
        }
    }
}

// Função de gerenciamento de memória
void gerenciamento_memoria()
{
    void *huge_page = alocar_huge_page();
    if (huge_page)
    {
        printf("Huge page alocada em %p\n", huge_page);
        liberar_huge_page(huge_page);
    }
    else
    {
        printf("Não foi possível alocar memória para huge page\n");
    }
}