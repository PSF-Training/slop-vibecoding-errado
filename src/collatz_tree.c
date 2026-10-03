/*
 * collatz_tree.c - Implementação da árvore de Collatz convergente.
 *
 * Como funciona:
 *   1. Para cada número escolhido, geramos o caminho de Collatz até 1.
 *   2. Cada valor distinto vira um nó único (deduplicado por valor).
 *   3. Ligamos cada nó ao próximo valor do seu caminho. Quando dois
 *      caminhos chegam ao mesmo valor, eles compartilham o nó seguinte:
 *      é a fusão que forma o tronco comum até 1.
 *   4. O nó "1" é a raiz/destino final.
 *
 * A busca de nós é feita por uma tabela hash aberta simples por valor,
 * para manter a construção linear mesmo com muitos caminhos.
 */
#include "collatz_tree.h"

#include <math.h>
#include <stdlib.h>
#include <string.h>

/* ------------------------------------------------------------------ */
/* Tabela hash de valores -> índice de nó                             */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t key; /* 0 = vazio */
    long index;
} HashSlot;

struct CollatzTreeBuilder {
    uint64_t *selected;
    size_t n_selected;
    HashSlot *slots;
    size_t slot_count;
};

static size_t hash_u64(uint64_t x)
{
    /* Mistura estilo splitmix64. */
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x = x ^ (x >> 31);
    return (size_t)x;
}

static int hash_init(CollatzTreeBuilder *b, size_t cap)
{
    size_t n = 16;
    while (n < cap * 2)
        n <<= 1;
    b->slots = calloc(n, sizeof(HashSlot));
    if (b->slots == NULL)
        return -1;
    b->slot_count = n;
    return 0;
}

static long hash_get(CollatzTreeBuilder *b, uint64_t key)
{
    size_t mask = b->slot_count - 1;
    size_t i = hash_u64(key) & mask;
    while (b->slots[i].key != 0) {
        if (b->slots[i].key == key)
            return b->slots[i].index;
        i = (i + 1) & mask;
    }
    return -1;
}

static void hash_put(CollatzTreeBuilder *b, uint64_t key, long index)
{
    size_t mask = b->slot_count - 1;
    size_t i = hash_u64(key) & mask;
    while (b->slots[i].key != 0)
        i = (i + 1) & mask;
    b->slots[i].key = key;
    b->slots[i].index = index;
}

/* ------------------------------------------------------------------ */
/* Árvore em construção                                               */
/* ------------------------------------------------------------------ */

typedef struct {
    CollatzTreeNode *nodes;
    size_t count;
    size_t capacity;
} NodeList;

static long add_node(NodeList *nl, uint64_t value, int depth, int is_leaf,
                     int leaf_color)
{
    if (nl->count == nl->capacity) {
        size_t nc = nl->capacity ? nl->capacity * 2 : 256;
        CollatzTreeNode *nn = realloc(nl->nodes, nc * sizeof(CollatzTreeNode));
        if (nn == NULL)
            return -1;
        nl->nodes = nn;
        nl->capacity = nc;
    }
    long idx = (long)nl->count++;
    CollatzTreeNode *n = &nl->nodes[idx];
    n->value = value;
    n->next = -1;
    n->depth = depth;
    n->is_leaf = is_leaf;
    n->leaf_color = leaf_color;
    n->column = -1;
    n->step = 0;
    n->path_len = 0;
    return idx;
}

/* Calcula a profundidade (passos até 1) de um valor, com 128 bits. */
static int value_depth(uint64_t n)
{
    int d = 0;
    __uint128_t x = n;
    while (x != 1) {
        if ((x & 1u) == 0u)
            x >>= 1;
        else {
            __uint128_t nx = 3u * x + 1u;
            if (nx > (__uint128_t)UINT64_MAX)
                break; /* não computável; interrompe com segurança */
            x = nx;
        }
        d++;
        if (d > 100000000)
            break;
    }
    return d;
}

/* ------------------------------------------------------------------ */
/* API                                                                */
/* ------------------------------------------------------------------ */

CollatzTreeBuilder *collatz_tree_builder_new(const uint64_t *selected,
                                             size_t n_selected)
{
    CollatzTreeBuilder *b = calloc(1, sizeof(CollatzTreeBuilder));
    if (b == NULL)
        return NULL;
    if (n_selected > 0) {
        b->selected = malloc(n_selected * sizeof(uint64_t));
        if (b->selected == NULL) {
            free(b);
            return NULL;
        }
        memcpy(b->selected, selected, n_selected * sizeof(uint64_t));
    }
    b->n_selected = n_selected;
    /* Estimativa de nós: ~200 passos por folha (folga). */
    size_t est = n_selected * 64 + 16;
    if (hash_init(b, est) != 0) {
        free(b->selected);
        free(b);
        return NULL;
    }
    return b;
}

int collatz_tree_build(CollatzTreeBuilder *b, CollatzTree *tree)
{
    if (b == NULL || tree == NULL)
        return -1;

    NodeList nl = {0};
    long root = -1;

    /* Garante que o nó 1 exista. */
    root = add_node(&nl, 1, 0, 0, -1);
    hash_put(b, 1, root);

    for (size_t s = 0; s < b->n_selected; s++) {
        uint64_t start = b->selected[s];
        if (start == 0)
            continue;

        /* Gera o caminho até 1 e cria/liga os nós. */
        uint64_t x = start;
        int depth = value_depth(start);
        long prev = -1; /* nó anterior (mais próximo das folhas) */

        /* Cria o nó da folha (se já existir, marca como folha). */
        long cur = hash_get(b, x);
        if (cur < 0) {
            cur = add_node(&nl, x, depth, 1, (int)s);
            hash_put(b, x, cur);
        } else {
            nl.nodes[cur].is_leaf = 1;
            nl.nodes[cur].leaf_color = (int)s;
        }
        /* A folha sempre revela a partir do próprio passo 0, mesmo que
         * já existisse por ser parte do caminho de outra folha. */
        nl.nodes[cur].column = (int)s;
        nl.nodes[cur].step = 0;
        nl.nodes[cur].path_len = depth + 1;
        prev = cur;

        int step = 0;
        /* Desce o caminho. */
        while (x != 1) {
            __uint128_t nx;
            if ((x & 1u) == 0u)
                nx = (__uint128_t)x >> 1;
            else
                nx = 3u * (__uint128_t)x + 1u;
            if (nx > (__uint128_t)UINT64_MAX)
                break; /* overflow: para com segurança */
            uint64_t next_val = (uint64_t)nx;
            depth--;
            step++;

            long nn = hash_get(b, next_val);
            if (nn < 0) {
                nn = add_node(&nl, next_val, depth <= 0 ? 0 : depth, 0, -1);
                hash_put(b, next_val, nn);
            }
            if (nl.nodes[nn].column < 0) {
                nl.nodes[nn].column = (int)s;
                nl.nodes[nn].step = step;
                nl.nodes[nn].path_len = step + (depth > 0 ? depth : 0) + 1;
            }

            /* Liga prev -> nn apenas se ainda não houver sucessor. */
            if (prev >= 0 && nl.nodes[prev].next < 0)
                nl.nodes[prev].next = nn;

            prev = nn;
            x = next_val;
            if (x == 1)
                break;
        }

        /* Liga o penúltimo nó ao 1, se necessário. */
        if (prev >= 0 && prev != root && nl.nodes[prev].next < 0)
            nl.nodes[prev].next = root;
    }

    tree->nodes = nl.nodes;
    tree->count = nl.count;
    tree->capacity = nl.capacity;
    tree->root_index = root;
    return 0;
}

void collatz_tree_builder_free(CollatzTreeBuilder *b)
{
    if (b == NULL)
        return;
    free(b->selected);
    free(b->slots);
    free(b);
}

void collatz_tree_free(CollatzTree *tree)
{
    if (tree == NULL)
        return;
    free(tree->nodes);
    tree->nodes = NULL;
    tree->count = 0;
    tree->capacity = 0;
    tree->root_index = -1;
}

/* ------------------------------------------------------------------ */
/* Layout em colunas em zigue-zague                                   */
/* ------------------------------------------------------------------ */

int collatz_tree_layout(const CollatzTree *tree, CollatzLayout **layout_out,
                        int wrap_len)
{
    if (tree == NULL || layout_out == NULL || tree->count == 0)
        return -1;

    CollatzLayout *lay = calloc(tree->count, sizeof(CollatzLayout));
    if (lay == NULL)
        return -1;

    /* Passo 1: maior passo real entre todos os nós (eixo Y global) e
     * número de folhas (colunas iniciais). */
    int max_step = 1;
    int n_leaves = 0;
    for (size_t i = 0; i < tree->count; i++) {
        if (tree->nodes[i].step > max_step)
            max_step = tree->nodes[i].step;
        if (tree->nodes[i].is_leaf)
            n_leaves++;
    }
    if (n_leaves < 1)
        n_leaves = 1;

    /* Passo 2: X pela coluna de origem. Cada folha tem uma coluna; um nó
     * compartilhado usa a coluna da primeira folha que o originou (nd->column).
     * Assim cada caminho desce na sua própria coluna e as fusões aparecem
     * como encontros entre colunas (desenhados como curvas suaves). */
    for (size_t i = 0; i < tree->count; i++) {
        int col = tree->nodes[i].column;
        if (col < 0)
            col = 0;
        if (col > n_leaves - 1)
            col = n_leaves - 1;
        lay[i].x = (n_leaves > 1)
                       ? (double)col / (double)(n_leaves - 1)
                       : 0.5;
    }

    /* Passo 4: Y global (passo real) e pequena oscilação lateral pela
     * paridade para dar o efeito de zigue-zague. */
    for (size_t i = 0; i < tree->count; i++) {
        const CollatzTreeNode *nd = &tree->nodes[i];
        int st = nd->step;
        if (st < 0) st = 0;

        double frac;
        if (wrap_len > 0 && max_step > wrap_len) {
            /* Comprime caminhos longos: aplica tangente hiperbólica para
             * aproximar os passos distantes sem estourar a tela. */
            double t = (double)st / (double)max_step;
            frac = t;
            /* Compressão suave além de wrap_len. */
            double cutoff = (double)wrap_len / (double)max_step;
            if (t > cutoff) {
                frac = cutoff + (1.0 - cutoff) *
                       (1.0 - exp(-(t - cutoff) * 3.0)) /
                       (1.0 - exp(-(1.0 - cutoff) * 3.0));
            }
        } else {
            frac = (max_step > 0) ? (double)st / (double)max_step : 0.0;
        }
        if (frac < 0.0) frac = 0.0;
        if (frac > 1.0) frac = 1.0;

        /* Oscilação lateral discreta (par/ímpar) para o zigue-zague. */
        double wobble = (nd->value & 1u) ? -0.012 : 0.012;
        double x = lay[i].x + wobble;
        if (x < 0.0) x = 0.0;
        if (x > 1.0) x = 1.0;

        lay[i].y = frac;
        lay[i].x = x;
        lay[i].step = st;
        lay[i].path_len = nd->path_len > 0 ? nd->path_len : max_step;
        lay[i].column = nd->column;
    }

    /* O nó 1 é o destino comum: fixa na base central. */
    if (tree->root_index >= 0 && (size_t)tree->root_index < tree->count) {
        lay[tree->root_index].x = 0.5;
        lay[tree->root_index].y = 1.0;
    }

    *layout_out = lay;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Caminhos independentes para desenho em colunas                     */
/* ------------------------------------------------------------------ */

void collatz_paths_free(CollatzPaths *paths)
{
    if (paths == NULL)
        return;
    if (paths->paths != NULL) {
        for (size_t i = 0; i < paths->count; i++)
            free(paths->paths[i].points);
        free(paths->paths);
    }
    paths->paths = NULL;
    paths->count = 0;
    paths->max_step = 0;
    paths->wrapped = 0;
}

int collatz_paths_build(const uint64_t *selected, size_t n_selected,
                        int wrap_len, CollatzPaths *out)
{
    if (selected == NULL || out == NULL || n_selected == 0)
        return -1;

    out->paths = calloc(n_selected, sizeof(CollatzPath));
    if (out->paths == NULL)
        return -1;
    out->count = n_selected;
    out->max_step = 0;
    out->wrapped = 0;

    /* Primeiro, gera todos os caminhos e descobre o maior passo. */
    for (size_t i = 0; i < n_selected; i++) {
        uint64_t start = selected[i];
        /* Conta passos com segurança (128 bits). */
        int steps = 0;
        __uint128_t x = start;
        while (x != 1 && steps < 100000000) {
            if ((x & 1u) == 0u)
                x >>= 1;
            else {
                __uint128_t nx = 3u * x + 1u;
                if (nx > (__uint128_t)UINT64_MAX)
                    break;
                x = nx;
            }
            steps++;
        }
        out->paths[i].start = start;
        out->paths[i].count = (size_t)steps + 1;
        out->paths[i].points =
            malloc(out->paths[i].count * sizeof(CollatzPathPoint));
        if (out->paths[i].points == NULL) {
            collatz_paths_free(out);
            return -1;
        }
        if (steps > out->max_step)
            out->max_step = steps;
    }

    if (out->max_step < 1)
        out->max_step = 1;

    /* Agora materializa os pontos com coordenadas. */
    for (size_t i = 0; i < n_selected; i++) {
        uint64_t start = out->paths[i].start;
        double colx = (n_selected > 1)
                          ? (double)i / (double)(n_selected - 1)
                          : 0.5;

        __uint128_t x = start;
        size_t k = 0;
        uint64_t prev = start;
        int step = 0;
        int plen = (int)out->paths[i].count - 1; /* passos deste caminho */
        if (plen < 1) plen = 1;
        for (;;) {
            /* y normalizado pelo próprio caminho, para que todas as
             * colunas terminem alinhadas na base (o 1). */
            double t = (double)step / (double)plen;
            double frac = t;
            if (wrap_len > 0 && plen > wrap_len) {
                double cutoff = (double)wrap_len / (double)plen;
                if (t > cutoff) {
                    out->wrapped = 1;
                    frac = cutoff + (1.0 - cutoff) *
                           (1.0 - exp(-(t - cutoff) * 3.0)) /
                           (1.0 - exp(-(1.0 - cutoff) * 3.0));
                }
            }
            if (frac < 0.0) frac = 0.0;
            if (frac > 1.0) frac = 1.0;

            /* Oscilação lateral discreta pela paridade (zigue-zague). */
            double wobble = ((x & 1u) ? -0.012 : 0.012);
            double px = colx + wobble;
            if (px < 0.0) px = 0.0;
            if (px > 1.0) px = 1.0;

            out->paths[i].points[k].value = (uint64_t)x;
            out->paths[i].points[k].x = px;
            out->paths[i].points[k].y = frac;
            out->paths[i].points[k].step = step;
            out->paths[i].points[k].grew = (k > 0) && ((uint64_t)x > prev);
            k++;
            prev = (uint64_t)x;

            if (x == 1 || k >= out->paths[i].count)
                break;

            if ((x & 1u) == 0u) {
                x >>= 1;
            } else {
                __uint128_t nx = 3u * x + 1u;
                if (nx > (__uint128_t)UINT64_MAX)
                    break;
                x = nx;
            }
            step++;
        }
        out->paths[i].count = k;
    }

    return 0;
}

/* ------------------------------------------------------------------ */
/* Grafo de Collatz com fusões e layout em camadas (estilo Sugiyama)   */
/* ------------------------------------------------------------------ */

/*
 * O desenho é por CAMADAS horizontais: cada profundidade (distância até
 * o 1) é uma linha; os nós são distribuídos em X dentro da camada.
 * - A ordem inicial vem de um BFS a partir do 1.
 * - Refinamento por barycenter reduz cruzamentos (2 passadas).
 * - Nós compartilhados (fusões) aparecem uma única vez e ficam perto dos
 *   vizinhos que convergem, como na figura clássica da Collatz tree.
 */

typedef struct {
    size_t *items;
    size_t count;
    size_t cap;
} IdxList;

static int idxlist_push(IdxList *l, size_t v)
{
    if (l->count == l->cap) {
        size_t nc = l->cap ? l->cap * 2 : 4;
        size_t *ni = realloc(l->items, nc * sizeof(size_t));
        if (ni == NULL)
            return -1;
        l->items = ni;
        l->cap = nc;
    }
    l->items[l->count++] = v;
    return 0;
}

static int edge_exists(const CollatzGraphEdge *e, size_t n, size_t from,
                       size_t to)
{
    for (size_t i = 0; i < n; i++)
        if (e[i].from == from && e[i].to == to)
            return 1;
    return 0;
}

/* Comparador por valor, mantido fora (ordenação feita in-place). */

int collatz_graph_build(const uint64_t *selected, size_t n_selected,
                        CollatzGraph *out)
{
    if (selected == NULL || out == NULL || n_selected == 0)
        return -1;
    memset(out, 0, sizeof(*out));
    out->root_index = -1;

    /* 1. Constrói a árvore convergente (com nós únicos). */
    CollatzTreeBuilder *b = collatz_tree_builder_new(selected, n_selected);
    if (b == NULL)
        return -1;
    CollatzTree t;
    if (collatz_tree_build(b, &t) != 0) {
        collatz_tree_builder_free(b);
        return -1;
    }

    size_t n = t.count;
    out->nodes = calloc(n, sizeof(CollatzGraphNode));
    if (out->nodes == NULL) {
        collatz_tree_free(&t);
        collatz_tree_builder_free(b);
        return -1;
    }
    out->node_count = n;
    out->root_index = t.root_index;

    int max_depth = 0;
    for (size_t i = 0; i < n; i++) {
        out->nodes[i].value = t.nodes[i].value;
        out->nodes[i].depth = t.nodes[i].depth;
        out->nodes[i].is_leaf = t.nodes[i].is_leaf;
        out->nodes[i].leaf_color = t.nodes[i].leaf_color;
        if (t.nodes[i].depth > max_depth)
            max_depth = t.nodes[i].depth;
    }
    if (max_depth < 1)
        max_depth = 1;
    out->max_depth = max_depth;

    /* 2. Coleta as arestas únicas child -> parent (rumo ao 1). */
    out->edges = malloc(n * sizeof(CollatzGraphEdge));
    if (out->edges == NULL) {
        collatz_tree_free(&t);
        collatz_tree_builder_free(b);
        collatz_graph_free(out);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        long nx = t.nodes[i].next;
        if (nx < 0 || (size_t)nx >= n)
            continue;
        if (edge_exists(out->edges, out->edge_count, i, (size_t)nx))
            continue;
        CollatzGraphEdge *e = &out->edges[out->edge_count++];
        e->from = i;
        e->to = (size_t)nx;
        e->grew = t.nodes[nx].value > t.nodes[i].value;
        e->highlighted = 0;
    }

    /* 3. Marca os caminhos das folhas escolhidas (destaque). */
    for (size_t s = 0; s < n_selected; s++) {
        long cur = -1;
        for (size_t i = 0; i < n; i++)
            if (t.nodes[i].value == selected[s]) { cur = (long)i; break; }
        if (cur < 0)
            continue;
        size_t guard = 0;
        while (cur >= 0 && (size_t)cur < n && guard++ < n + 5) {
            out->nodes[cur].highlighted = 1;
            long nx = t.nodes[cur].next;
            if (nx < 0 || (size_t)nx >= n)
                break;
            for (size_t k = 0; k < out->edge_count; k++)
                if (out->edges[k].from == (size_t)cur &&
                    out->edges[k].to == (size_t)nx)
                    out->edges[k].highlighted = 1;
            cur = nx;
        }
    }

    /* 4. Grau de entrada: nós de tronco (várias entradas). */
    for (size_t i = 0; i < out->edge_count; i++) {
        size_t to = out->edges[i].to;
        int indeg = 0;
        for (size_t k = 0; k < out->edge_count; k++)
            if (out->edges[k].to == to)
                indeg++;
        if (indeg > 1)
            out->nodes[to].is_trunk = 1;
    }

    /* 5. Listas de pais (entradas) e filhos (saídas) por nó. */
    IdxList *parents = calloc(n, sizeof(IdxList));  /* quem aponta para i */
    IdxList *children = calloc(n, sizeof(IdxList)); /* para quem i aponta  */
    if (parents == NULL || children == NULL) {
        free(parents); free(children);
        collatz_tree_free(&t);
        collatz_tree_builder_free(b);
        collatz_graph_free(out);
        return -1;
    }
    for (size_t e = 0; e < out->edge_count; e++) {
        idxlist_push(&parents[out->edges[e].to], out->edges[e].from);
        idxlist_push(&children[out->edges[e].from], out->edges[e].to);
    }

    /* 6. Camadas por profundidade. */
    size_t *layer_count = calloc((size_t)max_depth + 2, sizeof(size_t));
    size_t **layers = calloc((size_t)max_depth + 2, sizeof(size_t *));
    if (layer_count == NULL || layers == NULL) {
        free(layer_count); free(layers);
        for (size_t i = 0; i < n; i++) { free(parents[i].items); free(children[i].items); }
        free(parents); free(children);
        collatz_tree_free(&t);
        collatz_tree_builder_free(b);
        collatz_graph_free(out);
        return -1;
    }
    for (size_t i = 0; i < n; i++) {
        int d = out->nodes[i].depth;
        if (d < 0) d = 0;
        if (d > max_depth) d = max_depth;
        layer_count[d]++;
    }
    for (int d = 0; d <= max_depth; d++) {
        layers[d] = malloc((layer_count[d] ? layer_count[d] : 1) * sizeof(size_t));
        layer_count[d] = 0;
    }
    for (size_t i = 0; i < n; i++) {
        int d = out->nodes[i].depth;
        if (d < 0) d = 0;
        if (d > max_depth) d = max_depth;
        layers[d][layer_count[d]++] = i;
    }

    /* Ordena cada camada por valor descendente: valores maiores (que
     * dividem menos) tendem à esquerda, como na figura. */
    for (int d = 0; d <= max_depth; d++) {
        for (size_t a = 0; a < layer_count[d]; a++) {
            for (size_t bb = a + 1; bb < layer_count[d]; bb++) {
                if (out->nodes[layers[d][bb]].value >
                    out->nodes[layers[d][a]].value) {
                    size_t tmp = layers[d][a];
                    layers[d][a] = layers[d][bb];
                    layers[d][bb] = tmp;
                }
            }
        }
    }

    /* 7. Índice de posição (rank) dentro da camada, para barycenter. */
    size_t *rank = calloc(n, sizeof(size_t));
    for (int d = 0; d <= max_depth; d++)
        for (size_t a = 0; a < layer_count[d]; a++)
            rank[layers[d][a]] = a;

    /* Refinamento por barycenter: 4 passadas subindo/descendo. */
    for (int pass = 0; pass < 4; pass++) {
        if ((pass & 1) == 0) {
            /* Descendo (da raiz para as folhas): usa os pais. */
            for (int d = 1; d <= max_depth; d++) {
                for (size_t a = 0; a < layer_count[d]; a++) {
                    size_t idx = layers[d][a];
                    double sum = 0.0;
                    for (size_t k = 0; k < parents[idx].count; k++)
                        sum += (double)rank[parents[idx].items[k]];
                    if (parents[idx].count > 0)
                        rank[idx] = 0; /* marcador; recalculado abaixo */
                    out->nodes[idx].x = (parents[idx].count > 0)
                        ? sum / (double)parents[idx].count
                        : (double)a;
                }
                /* Reordena a camada pela chave x calculada. */
                for (size_t x1 = 0; x1 < layer_count[d]; x1++)
                    for (size_t x2 = x1 + 1; x2 < layer_count[d]; x2++)
                        if (out->nodes[layers[d][x2]].x <
                            out->nodes[layers[d][x1]].x) {
                            size_t tmp = layers[d][x1];
                            layers[d][x1] = layers[d][x2];
                            layers[d][x2] = tmp;
                        }
                for (size_t a = 0; a < layer_count[d]; a++)
                    rank[layers[d][a]] = a;
            }
        } else {
            /* Subindo (das folhas para a raiz): usa os filhos. */
            for (int d = max_depth - 1; d >= 0; d--) {
                for (size_t a = 0; a < layer_count[d]; a++) {
                    size_t idx = layers[d][a];
                    double sum = 0.0;
                    for (size_t k = 0; k < children[idx].count; k++)
                        sum += (double)rank[children[idx].items[k]];
                    out->nodes[idx].x = (children[idx].count > 0)
                        ? sum / (double)children[idx].count
                        : (double)a;
                }
                for (size_t x1 = 0; x1 < layer_count[d]; x1++)
                    for (size_t x2 = x1 + 1; x2 < layer_count[d]; x2++)
                        if (out->nodes[layers[d][x2]].x <
                            out->nodes[layers[d][x1]].x) {
                            size_t tmp = layers[d][x1];
                            layers[d][x1] = layers[d][x2];
                            layers[d][x2] = tmp;
                        }
                for (size_t a = 0; a < layer_count[d]; a++)
                    rank[layers[d][a]] = a;
            }
        }
    }

    /* 8. Atribui X final: posição uniforme dentro da camada. */
    for (int d = 0; d <= max_depth; d++) {
        size_t cnt = layer_count[d];
        for (size_t a = 0; a < cnt; a++) {
            size_t idx = layers[d][a];
            double frac = (cnt > 1) ? (double)a / (double)(cnt - 1) : 0.5;
            out->nodes[idx].x = frac;
            /* Y: profundidade 0 = base (1 embaixo), maior = topo. */
            out->nodes[idx].y = 1.0 - (double)out->nodes[idx].depth /
                                        (double)max_depth;
        }
    }

    for (size_t i = 0; i < n; i++) {
        free(parents[i].items);
        free(children[i].items);
    }
    free(parents);
    free(children);
    for (int d = 0; d <= max_depth; d++)
        free(layers[d]);
    free(layers);
    free(layer_count);
    free(rank);

    collatz_tree_free(&t);
    collatz_tree_builder_free(b);
    return 0;
}

void collatz_graph_free(CollatzGraph *graph)
{
    if (graph == NULL)
        return;
    free(graph->nodes);
    free(graph->edges);
    graph->nodes = NULL;
    graph->edges = NULL;
    graph->node_count = 0;
    graph->edge_count = 0;
    graph->root_index = -1;
    graph->max_depth = 0;
}
