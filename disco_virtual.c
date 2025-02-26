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

// Definição para Huge Page
#define HUGE_PAGE_SIZE (2 * 1024 * 1024)               // 2 MB
#define CAPACIDADE (HUGE_PAGE_SIZE / sizeof(uint32_t)) // 524288 elementos

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

FileSystem fs;
int disk_fd;

// Inicializa o sistema de arquivos
void initialize_filesystem()
{
    fs.file_count = 0; // inicia o contador como 0, já que não tem nenhum arquivo criado
    memset(fs.files, 0, sizeof(fs.files));
    memset(fs.free_blocks, 0, sizeof(fs.free_blocks));
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

    // Calcula o número de blocos necessários
    int blocks_needed = (tam * sizeof(uint32_t) + BLOCK_SIZE - 1) / BLOCK_SIZE;
    int total_blocks = DISK_SIZE / BLOCK_SIZE; // Total de blocos no sistema
    int start_block = -1;

    // Procura blocos contíguos livres
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

    // Marca blocos como ocupados
    for (int i = start_block; i < start_block + blocks_needed; i++)
    {
        fs.free_blocks[i] = 1;
    }

    // Cria a entrada do arquivo
    strncpy(fs.files[fs.file_count].name, nome, FILE_NAME_SIZE);
    fs.files[fs.file_count].size = tam * sizeof(uint32_t); // Usa uint32_t
    fs.files[fs.file_count].start_block = start_block;
    fs.file_count++;

    // Gera números aleatórios de 32 bits e escreve no disco
    lseek(disk_fd, start_block * BLOCK_SIZE, SEEK_SET);
    for (int i = 0; i < tam; i++)
    {
        uint32_t num = 0;
        // Preenche os 32 bits (4 bytes) com valores aleatórios
        for (int j = 0; j < 4; j++)
        {
            num = (num << 8) | (rand() % 256); // Byte aleatório (0-255)
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

/* Função auxiliar: realiza o merge de dois arquivos de runs ordenados.
   Utiliza a Huge Page (huge_buffer) como buffer de saída com capacidade 'capacidade' (em número de inteiros).
   Retorna (via strdup) o nome do arquivo temporário resultante do merge. */
static char *merge_two_runs(const char *file1, const char *file2, int32_t *huge_buffer, size_t capacidade)
{
    int fd1 = open(file1, O_RDONLY);
    int fd2 = open(file2, O_RDONLY);
    if (fd1 == -1 || fd2 == -1)
    {
        perror("Erro ao abrir arquivo para merge");
        if (fd1 != -1)
            close(fd1);
        if (fd2 != -1)
            close(fd2);
        return NULL;
    }
    char template[] = "/tmp/runXXXXXX";
    int out_fd = mkstemp(template);
    if (out_fd == -1)
    {
        perror("Erro ao criar arquivo temporário para merge");
        close(fd1);
        close(fd2);
        return NULL;
    }
    size_t out_index = 0;
    int32_t a, b;
    int tem_a = (read(fd1, &a, sizeof(int32_t)) == sizeof(int32_t));
    int tem_b = (read(fd2, &b, sizeof(int32_t)) == sizeof(int32_t));
    while (tem_a && tem_b)
    {
        if (a <= b)
        {
            huge_buffer[out_index++] = a;
            if (out_index == capacidade)
            {
                if (write(out_fd, huge_buffer, capacidade * sizeof(int32_t)) != (ssize_t)(capacidade * sizeof(int32_t)))
                {
                    perror("Erro ao escrever run merged");
                }
                out_index = 0;
            }
            tem_a = (read(fd1, &a, sizeof(int32_t)) == sizeof(int32_t));
        }
        else
        {
            huge_buffer[out_index++] = b;
            if (out_index == capacidade)
            {
                if (write(out_fd, huge_buffer, capacidade * sizeof(int32_t)) != (ssize_t)(capacidade * sizeof(int32_t)))
                {
                    perror("Erro ao escrever run merged");
                }
                out_index = 0;
            }
            tem_b = (read(fd2, &b, sizeof(int32_t)) == sizeof(int32_t));
        }
    }
    while (tem_a)
    {
        huge_buffer[out_index++] = a;
        if (out_index == capacidade)
        {
            if (write(out_fd, huge_buffer, capacidade * sizeof(int32_t)) != (ssize_t)(capacidade * sizeof(int32_t)))
            {
                perror("Erro ao escrever run merged");
            }
            out_index = 0;
        }
        tem_a = (read(fd1, &a, sizeof(int32_t)) == sizeof(int32_t));
    }
    while (tem_b)
    {
        huge_buffer[out_index++] = b;
        if (out_index == capacidade)
        {
            if (write(out_fd, huge_buffer, capacidade * sizeof(int32_t)) != (ssize_t)(capacidade * sizeof(int32_t)))
            {
                perror("Erro ao escrever run merged");
            }
            out_index = 0;
        }
        tem_b = (read(fd2, &b, sizeof(int32_t)) == sizeof(int32_t));
    }
    if (out_index > 0)
    {
        if (write(out_fd, huge_buffer, out_index * sizeof(int32_t)) != (ssize_t)(out_index * sizeof(int32_t)))
        {
            perror("Erro ao escrever run merged");
        }
    }
    close(fd1);
    close(fd2);
    close(out_fd);
    return strdup(template);
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
    int total_bytes = file->size;
    int total_elementos = total_bytes / sizeof(int32_t);

    // Aloca a Huge Page para uso exclusivo na ordenação
    int32_t *huge_buffer = (int32_t *)alocar_huge_page();
    if (!huge_buffer)
    {
        printf("Falha ao alocar memória para ordenação. Operação cancelada.\n");
        return;
    }

    // Ordenação in-memory (se o arquivo couber na Huge Page)
    if (total_elementos <= CAPACIDADE)
    {
        lseek(disk_fd, file->start_block * BLOCK_SIZE, SEEK_SET);
        if (read(disk_fd, huge_buffer, total_bytes) != total_bytes)
        {
            perror("Erro ao ler arquivo");
            liberar_huge_page(huge_buffer);
            return;
        }
        clock_t inicio = clock();
        qsort(huge_buffer, total_elementos, sizeof(int32_t), comparar_int32);
        clock_t fim = clock();
        lseek(disk_fd, file->start_block * BLOCK_SIZE, SEEK_SET);
        if (write(disk_fd, huge_buffer, total_bytes) != total_bytes)
        {
            perror("Erro ao escrever arquivo ordenado");
        }
        double tempo = (double)(fim - inicio) * 1000 / CLOCKS_PER_SEC;
        printf("Arquivo '%s' ordenado em %.2f ms.\n", nome, tempo);
        liberar_huge_page(huge_buffer);
        return;
    }

    // Ordenação externa usando paginação (arquivo maior que a capacidade da Huge Page)
    clock_t inicio = clock();
    int num_runs = (total_elementos + CAPACIDADE - 1) / CAPACIDADE;
    char **run_files = malloc(num_runs * sizeof(char *));
    if (!run_files)
    {
        perror("Erro de alocação");
        liberar_huge_page(huge_buffer);
        return;
    }
    // Cria as runs ordenadas
    for (int i = 0; i < num_runs; i++)
    {
        int elem_run = (i < num_runs - 1) ? CAPACIDADE : (total_elementos - i * CAPACIDADE);
        off_t offset = file->start_block * BLOCK_SIZE + i * CAPACIDADE * sizeof(int32_t);
        lseek(disk_fd, offset, SEEK_SET);
        if (read(disk_fd, huge_buffer, elem_run * sizeof(int32_t)) != elem_run * sizeof(int32_t))
        {
            perror("Erro ao ler para run");
            for (int j = 0; j < i; j++)
                free(run_files[j]);
            free(run_files);
            liberar_huge_page(huge_buffer);
            return;
        }
        qsort(huge_buffer, elem_run, sizeof(int32_t), comparar_int32);
        char template[] = "/tmp/runXXXXXX";
        int fd = mkstemp(template);
        if (fd == -1)
        {
            perror("Erro ao criar arquivo temporário");
            for (int j = 0; j < i; j++)
                free(run_files[j]);
            free(run_files);
            liberar_huge_page(huge_buffer);
            return;
        }
        if (write(fd, huge_buffer, elem_run * sizeof(int32_t)) != elem_run * sizeof(int32_t))
        {
            perror("Erro ao escrever run");
            close(fd);
            for (int j = 0; j < i; j++)
                free(run_files[j]);
            free(run_files);
            liberar_huge_page(huge_buffer);
            return;
        }
        close(fd);
        run_files[i] = strdup(template);
        if (!run_files[i])
        {
            perror("Erro ao duplicar nome de arquivo");
        }
    }
    // Merge externo: mescla as runs de duas em duas até obter uma única run final
    int runs_correntes = num_runs;
    while (runs_correntes > 1)
    {
        int new_runs = (runs_correntes + 1) / 2;
        char **new_run_files = malloc(new_runs * sizeof(char *));
        if (!new_run_files)
        {
            perror("Erro de alocação durante merge");
            break;
        }
        int new_index = 0;
        for (int i = 0; i < runs_correntes; i += 2)
        {
            if (i + 1 < runs_correntes)
            {
                char *merged = merge_two_runs(run_files[i], run_files[i + 1], huge_buffer, CAPACIDADE);
                unlink(run_files[i]);
                unlink(run_files[i + 1]);
                free(run_files[i]);
                free(run_files[i + 1]);
                new_run_files[new_index++] = merged;
            }
            else
            {
                new_run_files[new_index++] = run_files[i];
            }
        }
        free(run_files);
        run_files = new_run_files;
        runs_correntes = new_runs;
    }
    // Escreve o arquivo ordenado de volta no disco virtual
    int final_fd = open(run_files[0], O_RDONLY);
    if (final_fd == -1)
    {
        perror("Erro ao abrir run final");
        free(run_files[0]);
        free(run_files);
        liberar_huge_page(huge_buffer);
        return;
    }
    lseek(disk_fd, file->start_block * BLOCK_SIZE, SEEK_SET);
    ssize_t bytes_lidos;
    while ((bytes_lidos = read(final_fd, huge_buffer, HUGE_PAGE_SIZE)) > 0)
    {
        if (write(disk_fd, huge_buffer, bytes_lidos) != bytes_lidos)
        {
            perror("Erro ao escrever arquivo final no disco");
            break;
        }
    }
    close(final_fd);
    unlink(run_files[0]);
    free(run_files[0]);
    free(run_files);
    clock_t fim = clock();
    double tempo = (double)(fim - inicio) * 1000 / CLOCKS_PER_SEC;
    printf("Arquivo '%s' ordenado em %.2f ms (externo).\n", nome, tempo);
    liberar_huge_page(huge_buffer);
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
    // Cria ou abre o arquivo de disco virtual
    disk_fd = open("disco_virtual.img", O_RDWR | O_CREAT, 0644);
    if (disk_fd < 0)
    {
        perror("Erro ao abrir o disco virtual");
        exit(EXIT_FAILURE);
    }

    // Verifica o tamanho do arquivo
    off_t size = lseek(disk_fd, 0, SEEK_END);

    // Se for um novo arquivo, expande para o tamanho desejado
    if (size < DISK_SIZE)
    {
        printf("Inicializando disco virtual com tamanho de 1GB...\n");
        // Posiciona no final do arquivo
        lseek(disk_fd, DISK_SIZE - 1, SEEK_SET);
        // Escreve um byte para forçar a alocação do espaço
        char zero = 0;
        write(disk_fd, &zero, 1);
    }

    // Volta para o início do arquivo
    lseek(disk_fd, 0, SEEK_SET);

    // Inicializa o sistema de arquivos
    initialize_filesystem();

    printf("Sistema de arquivos inicializado.\n");
}