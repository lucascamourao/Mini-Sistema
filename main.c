#include <stdio.h>
#include <stdbool.h>

// Declarações das funções
void sistema_arquivos();
void gerenciamento_memoria();
void criar(const char *nome, int tam);
void apagar(const char *nome);
void listar();
void ordenar(const char *nome);
void ler(const char *nome, int inicio, int fim);
void concatenar(const char *nome1, const char *nome2);

int main()
{
    int escolha;

    // Inicializa o sistema de arquivos
    srand(time(NULL));
    sistema_arquivos();

    while (true)
    {
        // Menu de opções
        printf("\n--- Menu ---\n");
        printf("1 - Criar arquivo\n");
        printf("2 - Apagar arquivo\n");
        printf("3 - Listar arquivos no diretório\n");
        printf("4 - Ordenar a lista no arquivo\n");
        printf("5 - Exibir sublista de um arquivo\n");
        printf("6 - Concatenar dois arquivos\n");
        printf("0 - Sair\n");
        printf("Escolha uma opção: ");
        scanf("%d", &escolha);

        // Verifica se a escolha é válida
        if (escolha < 0 || escolha > 6)
        {
            printf("Opção inválida! Tente novamente.\n");
            continue;
        }

        // Finaliza o programa
        if (escolha == 0)
        {
            printf("Saindo...\n");
            break;
        }

        // Executa a função correspondente à escolha
        switch (escolha)
        {
        case 1:
        {
            char nome[32];
            int tam;
            printf("Digite o nome do arquivo: ");
            scanf("%s", nome);
            printf("Digite o tamanho do arquivo (quantidade de números): ");
            scanf("%d", &tam);
            criar(nome, tam);
            break;
        }
        case 2:
        {
            char nome[32];
            printf("Digite o nome do arquivo: ");
            scanf("%s", nome);
            apagar(nome);
            break;
        }
        case 3:
            listar();
            break;
        case 4:
        {
            char nome[32];
            printf("Digite o nome do arquivo: ");
            scanf("%s", nome);
            ordenar(nome);
            break;
        }
        case 5:
        {
            char nome[32];
            int inicio, fim;
            printf("Digite o nome do arquivo: ");
            scanf("%s", nome);
            printf("Digite o início e o fim do intervalo: ");
            scanf("%d %d", &inicio, &fim);
            ler(nome, inicio, fim);
            break;
        }
        case 6:
        {
            char nome1[32], nome2[32];
            printf("Digite o nome do primeiro arquivo: ");
            scanf("%s", nome1);
            printf("Digite o nome do segundo arquivo: ");
            scanf("%s", nome2);
            concatenar(nome1, nome2);
            break;
        }
        }
    }

    return 0;
}