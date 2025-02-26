#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdint.h>
#include <time.h>
#include <sys/mman.h>
#include <linux/mman.h>

#define DISK_SIZE 1073741824 // 1 GB
#define BLOCK_SIZE 4096
#define FILE_NAME_SIZE 32
#define MAX_FILES 1024
#define SWAP_SIZE 104857600 // 100 MB para área de swap

// Definições para Huge Page
#define HUGE_PAGE_SIZE (2 * 1024 * 1024)
#define CAPACIDADE (HUGE_PAGE_SIZE / sizeof(uint32_t))

// Declarações externas para as funções de gerenciamento de huge page (implementadas em memoria.c)
extern void *alocar_huge_page();
extern void liberar_huge_page(void *page);

// Estrutura de um arquivo
typedef struct
{
    char name[FILE_NAME_SIZE];
    int size;        // Tamanho em bytes
    int start_block; // Bloco inicial no disco
} FileEntry;

// Estrutura do sistema de arquivos
typedef struct
{
    FileEntry files[MAX_FILES];
    int file_count;                          // Número de arquivos atualmente no sistema
    int free_blocks[DISK_SIZE / BLOCK_SIZE]; // Bitmap de blocos livres
} FileSystem;

typedef struct
{
    int start_block;
    int num_blocks;
    int num_elements;
} RunInfo;

FileSystem fs;
int disk_fd;

void verificar_config_hugepage()
{
    printf("\nERRO: Configuração necessária:\n");
    printf("1. Reserve 1 Huge Page (2MB):\n");
    printf("   sudo sysctl vm.nr_hugepages=1\n");
    printf("2. Verifique permissões no diretório /dev/hugepages\n\n");
}

// Inicializa o sistema de arquivos
void initialize_filesystem()
{
    fs.file_count = 0;
    memset(fs.files, 0, sizeof(fs.files));
    memset(fs.free_blocks, 0, sizeof(fs.free_blocks));

    // Reservar área de swap
    int swap_start_block = (DISK_SIZE - SWAP_SIZE) / BLOCK_SIZE;
    int swap_blocks = SWAP_SIZE / BLOCK_SIZE;
    for (int i = swap_start_block; i < swap_start_block + swap_blocks; i++)
    {
        fs.free_blocks[i] = 1;
    }
}

int allocate_swap_blocks(int blocks_needed)
{
    int swap_start = (DISK_SIZE - SWAP_SIZE) / BLOCK_SIZE;
    for (int i = swap_start; i <= (DISK_SIZE / BLOCK_SIZE) - blocks_needed; i++)
    {
        int j;
        for (j = i; j < i + blocks_needed; j++)
        {
            if (fs.free_blocks[j] != 0)
                break;
        }
        if (j == i + blocks_needed)
        {
            for (int k = i; k < i + blocks_needed; k++)
                fs.free_blocks[k] = 1;
            return i;
        }
    }
    return -1;
}

void free_swap_blocks(int start_block, int num_blocks)
{
    for (int i = start_block; i < start_block + num_blocks; i++)
    {
        fs.free_blocks[i] = 0;
    }
}

/* Cria um arquivo com uma lista aleatória de números inteiros positivos de 32 bits.
O argumento "tam" indica a quantidade de números. */
void criar(const char *nome, int tam)
{
    if (fs.file_count >= MAX_FILES)
    {
        printf("Número máximo de arquivos atingido.\n");
        return;
    }

    int blocks_needed = (tam * sizeof(uint32_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int total_blocks = (DISK_SIZE - SWAP_SIZE) / BLOCK_SIZE; // Ignorar área de swap
    int start_block = -1;

    for (int i = 0; i <= total_blocks - blocks_needed; i++)
    {
        int j;
        for (j = i; j < i + blocks_needed; j++)
        {
            if (fs.free_blocks[j] != 0)
                break;
        }
        if (j == i + blocks_needed)
        {
            start_block = i;
            break;
        }
    }

    if (start_block == -1)
    {
        printf("Espaço insuficiente no disco.\n");
        return;
    }

    for (int i = start_block; i < start_block + blocks_needed; i++)
    {
        fs.free_blocks[i] = 1;
    }

    strncpy(fs.files[fs.file_count].name, nome, FILE_NAME_SIZE);
    fs.files[fs.file_count].size = tam * sizeof(uint32_t);
    fs.files[fs.file_count].start_block = start_block;
    fs.file_count++;

    lseek(disk_fd, start_block * BLOCK_SIZE, SEEK_SET);
    for (int i = 0; i < tam; i++)
    {
        uint32_t num = 0;
        for (int j = 0; j < 4; j++)
        {
            num = (num << 8) | (rand() % 256);
        }
        write(disk_fd, &num, sizeof(uint32_t));
    }

    printf("Arquivo '%s' criado com sucesso.\n", nome);
}

// Apaga um arquivo
void apagar(const char *nome)
{
    for (int i = 0; i < fs.file_count; i++)
    {
        if (strcmp(fs.files[i].name, nome) == 0)
        {
            // Libera os blocos
            for (int j = fs.files[i].start_block; j < fs.files[i].start_block + (fs.files[i].size + BLOCK_SIZE - 1) / BLOCK_SIZE; j++)
            {
                fs.free_blocks[j] = 0;
            }
            // Remove a entrada do arquivo
            memmove(&fs.files[i], &fs.files[i + 1], (fs.file_count - i - 1) * sizeof(FileEntry));
            fs.file_count--;
            printf("Arquivo '%s' apagado com sucesso.\n", nome);
            return;
        }
    }
    printf("Arquivo '%s' não encontrado.\n", nome);
}

/* Lista os arquivos
Mostra, ao lado de cada arquivo, o seu tamanho em bytes.
Também mostra o espaço total do "disco" e o espaço disponível. */
void listar()
{
    printf("Arquivos no diretório:\n");
    for (int i = 0; i < fs.file_count; i++)
    {
        printf("%s\t%d bytes\n", fs.files[i].name, fs.files[i].size);
    }

    // Calcula o espaço total e o espaço disponível
    long espaco_total = DISK_SIZE;
    long espaco_usado = 0;

    for (int i = 0; i < fs.file_count; i++)
    {
        espaco_usado += fs.files[i].size;
    }

    long espaco_disponivel = espaco_total - espaco_usado;

    printf("\nEspaço total do disco: %ld bytes (%.2f MB)\n", espaco_total, (double)espaco_total / (1024 * 1024));
    printf("Espaço utilizado: %ld bytes (%.2f MB)\n", espaco_usado, (double)espaco_usado / (1024 * 1024));
    printf("Espaço disponível: %ld bytes (%.2f MB)\n", espaco_disponivel, (double)espaco_disponivel / (1024 * 1024));
}

// Função de comparação para int32_t (números com sinal)
int comparar_int32(const void *a, const void *b)
{
    int32_t n1 = *(const int32_t *)a;
    int32_t n2 = *(const int32_t *)b;
    return (n1 > n2) - (n1 < n2);
}

RunInfo merge_two_runs(RunInfo run1, RunInfo run2, int32_t *buffer)
{
    RunInfo merged;
    merged.num_elements = run1.num_elements + run2.num_elements;
    int bytes_needed = merged.num_elements * sizeof(int32_t);
    merged.num_blocks = (bytes_needed + BLOCK_SIZE - 1) / BLOCK_SIZE;

    merged.start_block = allocate_swap_blocks(merged.num_blocks);
    if (merged.start_block == -1)
    {
        merged.num_blocks = 0;
        return merged;
    }

    int32_t *data1 = malloc(run1.num_elements * sizeof(int32_t));
    int32_t *data2 = malloc(run2.num_elements * sizeof(int32_t));

    lseek(disk_fd, run1.start_block * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, data1, run1.num_elements * sizeof(int32_t));
    lseek(disk_fd, run2.start_block * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, data2, run2.num_elements * sizeof(int32_t));

    int i = 0, j = 0, k = 0;
    while (i < run1.num_elements && j < run2.num_elements)
    {
        buffer[k++] = (data1[i] <= data2[j]) ? data1[i++] : data2[j++];
    }
    while (i < run1.num_elements)
        buffer[k++] = data1[i++];
    while (j < run2.num_elements)
        buffer[k++] = data2[j++];

    lseek(disk_fd, merged.start_block * BLOCK_SIZE, SEEK_SET);
    write(disk_fd, buffer, bytes_needed);

    free_swap_blocks(run1.start_block, run1.num_blocks);
    free_swap_blocks(run2.start_block, run2.num_blocks);
    free(data1);
    free(data2);

    return merged;
}

/* Função ordenar:
   Ordena a lista de inteiros armazenada no arquivo cujo nome é passado em 'nome'.
   Se a quantidade de números couber na Huge Page (2MB), a ordenação é feita in-memory;
   caso contrário, é realizada uma ordenação externa usando paginação com runs temporárias.
   Ao final, o tempo gasto (em ms) é exibido. */
void ordenar(const char *nome)
{
    int file_idx = -1;
    for (int i = 0; i < fs.file_count; i++)
    {
        if (strcmp(fs.files[i].name, nome) == 0)
        {
            file_idx = i;
            break;
        }
    }
    if (file_idx == -1)
    {
        printf("Arquivo '%s' não encontrado.\n", nome);
        return;
    }

    FileEntry *file = &fs.files[file_idx];
    int total_elementos = file->size / sizeof(int32_t);
    int32_t *huge_buffer = (int32_t *)alocar_huge_page();

    if (!huge_buffer)
    {
        verificar_config_hugepage();
        printf("Falha crítica: Não foi possível alocar a Huge Page!\n");
        return;
    }

    int precisa_liberar = 1;

    if (total_elementos <= CAPACIDADE)
    {
        lseek(disk_fd, file->start_block * BLOCK_SIZE, SEEK_SET);
        read(disk_fd, huge_buffer, file->size);
        qsort(huge_buffer, total_elementos, sizeof(int32_t), comparar_int32);
        lseek(disk_fd, file->start_block * BLOCK_SIZE, SEEK_SET);
        write(disk_fd, huge_buffer, file->size);
        goto cleanup;
    }

    clock_t inicio = clock();
    int num_runs = (total_elementos + CAPACIDADE - 1) / CAPACIDADE;
    RunInfo *runs = malloc(num_runs * sizeof(RunInfo));

    for (int i = 0; i < num_runs; i++)
    {
        int elementos = (i == num_runs - 1) ? (total_elementos % CAPACIDADE) : CAPACIDADE;
        off_t offset = file->start_block * BLOCK_SIZE + (i * CAPACIDADE * sizeof(int32_t));

        lseek(disk_fd, offset, SEEK_SET);
        read(disk_fd, huge_buffer, elementos * sizeof(int32_t));
        qsort(huge_buffer, elementos, sizeof(int32_t), comparar_int32);

        int blocos = (elementos * sizeof(int32_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
        int bloco_inicial = allocate_swap_blocks(blocos);

        lseek(disk_fd, bloco_inicial * BLOCK_SIZE, SEEK_SET);
        write(disk_fd, huge_buffer, elementos * sizeof(int32_t));

        runs[i] = (RunInfo){bloco_inicial, blocos, elementos};
    }

    while (num_runs > 1)
    {
        int new_runs = (num_runs + 1) / 2;
        RunInfo *new_runs_arr = malloc(new_runs * sizeof(RunInfo));

        for (int i = 0; i < num_runs; i += 2)
        {
            if (i + 1 >= num_runs)
            {
                new_runs_arr[i / 2] = runs[i];
                continue;
            }
            new_runs_arr[i / 2] = merge_two_runs(runs[i], runs[i + 1], huge_buffer);
        }

        free(runs);
        runs = new_runs_arr;
        num_runs = new_runs;
    }

    if (runs[0].num_elements != total_elementos)
    {
        printf("Erro: Dados corrompidos durante a ordenação!\n");
        goto cleanup;
    }

    lseek(disk_fd, runs[0].start_block * BLOCK_SIZE, SEEK_SET);
    read(disk_fd, huge_buffer, runs[0].num_elements * sizeof(int32_t));
    lseek(disk_fd, file->start_block * BLOCK_SIZE, SEEK_SET);
    write(disk_fd, huge_buffer, file->size);

    free_swap_blocks(runs[0].start_block, runs[0].num_blocks);
    free(runs);

    printf("Ordenação concluída em %.2f ms\n",
           (double)(clock() - inicio) * 1000 / CLOCKS_PER_SEC);

cleanup:
    if (precisa_liberar)
    {
        liberar_huge_page(huge_buffer);
    }
}

void ler(const char *nome, int inicio, int fim)
{
    // Encontra o arquivo
    int file_index = -1;
    for (int i = 0; i < fs.file_count; i++)
    {
        if (strcmp(fs.files[i].name, nome) == 0)
        {
            file_index = i;
            break;
        }
    }
    if (file_index == -1)
    {
        printf("Arquivo '%s' não encontrado.\n", nome);
        return;
    }

    FileEntry *file = &fs.files[file_index];
    int num_inteiros = file->size / sizeof(uint32_t);

    // Valida o intervalo
    if (inicio < 0 || fim >= num_inteiros || inicio > fim)
    {
        printf("Intervalo inválido.\n");
        return;
    }

    // Calcula o offset no disco
    off_t offset = file->start_block * BLOCK_SIZE + inicio * sizeof(uint32_t);
    int tamanho_sublista = (fim - inicio + 1) * sizeof(uint32_t);

    // Aloca buffer e lê do disco
    uint32_t *buffer = malloc(tamanho_sublista);
    if (!buffer)
    {
        perror("Erro ao alocar memória para leitura");
        return;
    }

    lseek(disk_fd, offset, SEEK_SET);
    if (read(disk_fd, buffer, tamanho_sublista) != tamanho_sublista)
    {
        perror("Erro ao ler dados do arquivo");
        free(buffer);
        return;
    }

    // Exibe a sublista
    printf("Sublista de '%s' (%d a %d):\n", nome, inicio, fim);
    for (int i = 0; i <= fim - inicio; i++)
    {
        printf("%u ", buffer[i]);
    }
    printf("\n");

    free(buffer);
}

void concatenar(const char *nome1, const char *nome2)
{
    int file1_idx = -1, file2_idx = -1;
    for (int i = 0; i < fs.file_count; i++)
    {
        if (strcmp(fs.files[i].name, nome1) == 0)
            file1_idx = i;
        if (strcmp(fs.files[i].name, nome2) == 0)
            file2_idx = i;
    }
    if (file1_idx == -1 || file2_idx == -1)
    {
        printf("Arquivo(s) não encontrado(s).\n");
        return;
    }

    FileEntry *file1 = &fs.files[file1_idx];
    FileEntry *file2 = &fs.files[file2_idx];
    int total_size = file1->size + file2->size;

    // Aloca memória para os dados concatenados
    void *buffer = malloc(total_size);
    if (!buffer)
    {
        perror("Erro ao alocar memória para concatenação");
        return;
    }

    // Lê dados dos arquivos originais
    lseek(disk_fd, file1->start_block * BLOCK_SIZE, SEEK_SET);
    if (read(disk_fd, buffer, file1->size) != file1->size)
    {
        perror("Erro ao ler primeiro arquivo");
        free(buffer);
        return;
    }

    lseek(disk_fd, file2->start_block * BLOCK_SIZE, SEEK_SET);
    if (read(disk_fd, (uint8_t *)buffer + file1->size, file2->size) != file2->size)
    {
        perror("Erro ao ler segundo arquivo");
        free(buffer);
        return;
    }

    // Aloca espaço para o novo arquivo
    int blocos_necessarios = (total_size + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int bloco_inicial = -1;
    int total_blocks = DISK_SIZE / BLOCK_SIZE;

    // Procura blocos contíguos livres
    for (int i = 0; i < total_blocks - blocos_necessarios + 1; i++)
    {
        int j;
        for (j = i; j < i + blocos_necessarios; j++)
        {
            if (fs.free_blocks[j] != 0)
                break;
        }
        if (j == i + blocos_necessarios)
        {
            bloco_inicial = i;
            break;
        }
    }

    if (bloco_inicial == -1)
    {
        printf("Espaço insuficiente para o arquivo concatenado.\n");
        free(buffer);
        return;
    }

    // Marca os novos blocos como ocupados
    for (int i = bloco_inicial; i < bloco_inicial + blocos_necessarios; i++)
    {
        fs.free_blocks[i] = 1;
    }

    // Escreve os dados concatenados
    lseek(disk_fd, bloco_inicial * BLOCK_SIZE, SEEK_SET);
    if (write(disk_fd, buffer, total_size) != total_size)
    {
        perror("Erro ao escrever arquivo concatenado");
        free(buffer);
        return;
    }

    free(buffer);

    // Cria a entrada para o arquivo concatenado (reutilizando o nome do primeiro)
    char nome_concatenado[FILE_NAME_SIZE];
    strncpy(nome_concatenado, nome1, FILE_NAME_SIZE - 1);
    nome_concatenado[FILE_NAME_SIZE - 1] = '\0';

    // Libera os blocos dos arquivos originais
    for (int i = file1->start_block; i < file1->start_block + (file1->size + BLOCK_SIZE - 1) / BLOCK_SIZE; i++)
    {
        fs.free_blocks[i] = 0;
    }

    for (int i = file2->start_block; i < file2->start_block + (file2->size + BLOCK_SIZE - 1) / BLOCK_SIZE; i++)
    {
        fs.free_blocks[i] = 0;
    }

    // Remove as entradas originais do diretório
    // Remove primeiro o arquivo que vem depois no diretório para evitar deslocamentos incorretos
    if (file1_idx > file2_idx)
    {
        memmove(&fs.files[file1_idx], &fs.files[file1_idx + 1], (fs.file_count - file1_idx - 1) * sizeof(FileEntry));
        memmove(&fs.files[file2_idx], &fs.files[file2_idx + 1], (fs.file_count - file2_idx - 1) * sizeof(FileEntry));
        fs.file_count -= 2;
    }
    else
    {
        memmove(&fs.files[file2_idx], &fs.files[file2_idx + 1], (fs.file_count - file2_idx - 1) * sizeof(FileEntry));
        memmove(&fs.files[file1_idx], &fs.files[file1_idx + 1], (fs.file_count - file1_idx - 1) * sizeof(FileEntry));
        fs.file_count -= 2;
    }

    // Adiciona a nova entrada para o arquivo concatenado
    strncpy(fs.files[fs.file_count].name, nome_concatenado, FILE_NAME_SIZE);
    fs.files[fs.file_count].size = total_size;
    fs.files[fs.file_count].start_block = bloco_inicial;
    fs.file_count++;

    printf("Arquivos '%s' e '%s' concatenados em '%s'.\n", nome1, nome2, nome_concatenado);
}

// Inicializa o sistema de arquivos
void sistema_arquivos()
{
    disk_fd = open("disco_virtual.img", O_RDWR | O_CREAT, 0644);
    if (disk_fd < 0)
    {
        perror("Erro ao abrir disco virtual");
        exit(EXIT_FAILURE);
    }

    // Verificação inicial de Huge Page
    void *test_page = alocar_huge_page();
    if (!test_page)
    {
        verificar_config_hugepage();
        close(disk_fd);
        exit(EXIT_FAILURE);
    }
    liberar_huge_page(test_page);

    // Verificar tamanho do disco
    if (lseek(disk_fd, DISK_SIZE - 1, SEEK_SET) == -1)
    {
        perror("Erro ao configurar disco");
        close(disk_fd);
        exit(EXIT_FAILURE);
    }
    write(disk_fd, "", 1);

    initialize_filesystem();
    printf("Sistema inicializado. Huge Page configurada com sucesso.\n");
}