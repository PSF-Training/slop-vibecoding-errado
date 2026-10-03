/*
 * collatz_tree.h - Construção da árvore de Collatz a partir de números
 * escolhidos até o 1.
 *
 * Modelo: cada número escolhido é uma FOLHA (topo da árvore). De cada
 * folha desce o caminho de Collatz (n -> n/2 ou 3n+1) até 1. Caminhos
 * que passam pelos mesmos valores se FUNDEM, formando um grafo dirigido
 * acíclico cujo destino final é sempre 1.
 *
 * A árvore é guardada como um conjunto de nós únicos e, para cada nó, o
 * índice do seu sucessor (o próximo número do caminho, ou -1 para o nó 1).
 */
#ifndef COLLATZ_TREE_H
#define COLLATZ_TREE_H

#include <stddef.h>
#include <stdint.h>

/* Um nó da árvore: um valor da sequência. */
typedef struct {
    uint64_t value;   /* o número em si                                  */
    long next;        /* índice do sucessor no caminho, -1 se for o 1    */
    int depth;        /* distância até 1 (1 = 0 passos)                  */
    int is_leaf;      /* 1 se é um dos números escolhidos                */
    int leaf_color;   /* índice de cor (a folha de origem), -1 se nenhuma */
    int column;       /* coluna/folha que originou o nó (-1 se a raiz)   */
    int step;         /* passo no caminho da folha de origem             */
    int path_len;     /* comprimento do caminho da folha de origem       */
} CollatzTreeNode;

/* Estado de construção (opaco, uso interno). */
typedef struct CollatzTreeBuilder CollatzTreeBuilder;

typedef struct {
    CollatzTreeNode *nodes;
    size_t count;
    size_t capacity;
    long root_index;  /* índice do nó de valor 1 (o destino comum)       */
} CollatzTree;

/*
 * Um ponto de um caminho pronto para desenho.
 * Coordenadas normalizadas em [0,1]:
 *   x - coluna do caminho (0 = primeira folha, 1 = última)
 *   y - passo normalizado (0 = folha/topo, 1 = 1/base)
 */
typedef struct {
    uint64_t value;
    double x, y;
    int step;
    int grew; /* 1 se este passo cresceu (veio de 3n+1) */
} CollatzPathPoint;

/* O caminho completo de uma folha até 1. */
typedef struct {
    CollatzPathPoint *points;
    size_t count;
    uint64_t start;
} CollatzPath;

/* Coleção de caminhos (um por folha escolhida). */
typedef struct {
    CollatzPath *paths;
    size_t count;
    int max_step;   /* maior passo entre todos (para referência)  */
    int wrapped;    /* 1 se algum caminho foi comprimido          */
} CollatzPaths;

/*
 * Constrói os caminhos independentes de cada folha até 1, já com
 * coordenadas normalizadas para desenho em colunas.
 *   - x: cada folha recebe uma coluna uniforme em [0,1].
 *   - y: passo real normalizado globalmente, com compressão suave de
 *        trechos além de wrap_len (se wrap_len > 0).
 * Retorna 0 em sucesso; libere com collatz_paths_free().
 */
int collatz_paths_build(const uint64_t *selected, size_t n_selected,
                        int wrap_len, CollatzPaths *out);

void collatz_paths_free(CollatzPaths *paths);

/*
 * Layout por nó da árvore (versão com fusões). Mantido para estatísticas
 * e usos futuros; o app macOS usa collatz_paths_build().
 */
typedef struct {
    double x, y;
    int step;
    int path_len;
    int column;
} CollatzLayout;

/* ------------------------------------------------------------------ */
/* Grafo de Collatz com fusões (o desenho "de verdade" da árvore)      */
/* ------------------------------------------------------------------ */

/*
 * Um nó único do grafo. Valores que aparecem em mais de um caminho são
 * UM só nó, com várias arestas de entrada (é a fusão que forma o tronco
 * comum até o 1, como na figura clássica da Collatz tree).
 */
typedef struct {
    uint64_t value;
    double x, y;        /* posição normalizada em [0,1] (y=0 topo, 1 base) */
    int depth;          /* passos até o 1 (0 para o próprio 1)             */
    int is_leaf;        /* 1 se é um dos números digitados                 */
    int is_trunk;       /* 1 se recebe mais de uma entrada (fusão)         */
    int highlighted;    /* 1 se pertence a algum caminho destacado         */
    int leaf_color;     /* cor do ramo (folha de origem), -1 se nenhuma    */
} CollatzGraphNode;

/* Uma aresta dirigida: child -> parent (na direção do 1). */
typedef struct {
    size_t from;        /* índice do nó "de cima"                          */
    size_t to;          /* índice do nó seguinte (rumo ao 1)               */
    int grew;           /* 1 se 3n+1 (cresce), 0 se n/2 (cai)              */
    int highlighted;    /* 1 se a aresta pertence a um caminho destacado   */
} CollatzGraphEdge;

typedef struct {
    CollatzGraphNode *nodes;
    size_t node_count;
    CollatzGraphEdge *edges;
    size_t edge_count;
    long root_index;    /* índice do nó 1                                  */
    int max_depth;
} CollatzGraph;

/*
 * Constrói o grafo convergente dos números escolhidos, já com posições
 * prontas para desenho (estilo "árvore para baixo": folhas no topo, 1 na
 * base, ramos que se fundem num nó único). Os caminhos das folhas
 * escolhidas ficam marcados com highlighted=1 para destaque.
 * Retorna 0 em sucesso; libere com collatz_graph_free().
 */
int collatz_graph_build(const uint64_t *selected, size_t n_selected,
                        CollatzGraph *out);

void collatz_graph_free(CollatzGraph *graph);

/*
 * Calcula o layout padrão (colunas em zigue-zague).
 * Preenche layout_out, um vetor de tree->count posições.
 * wrap_len > 0 resume trechos mais longos que wrap_len passos, comprimindo
 * a escala vertical (o desenho marca esses trechos como tracejados).
 * Retorna 0 em sucesso, -1 em erro. Libere com free().
 */
int collatz_tree_layout(const CollatzTree *tree, CollatzLayout **layout_out,
                        int wrap_len);

/*
 * Cria um construtor de árvore.
 * selected     - vetor de números escolhidos (as folhas)
 * n_selected   - quantidade de números
 * Retorna NULL em erro.
 */
CollatzTreeBuilder *collatz_tree_builder_new(const uint64_t *selected,
                                             size_t n_selected);

/*
 * Constrói a árvore a partir dos números escolhidos.
 * Preenche *tree. Retorna 0 em sucesso, -1 em erro.
 * A árvore resultante deve ser liberada com collatz_tree_free().
 */
int collatz_tree_build(CollatzTreeBuilder *b, CollatzTree *tree);

void collatz_tree_builder_free(CollatzTreeBuilder *b);
void collatz_tree_free(CollatzTree *tree);

#endif /* COLLATZ_TREE_H */
