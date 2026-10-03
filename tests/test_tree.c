/*
 * test_tree.c - Valida a construção da árvore de Collatz convergente.
 *
 * Verifica que:
 *   - cada folha escolhida existe na árvore;
 *   - seguindo os sucessores de cada folha chega-se sempre em 1;
 *   - caminhos que compartilham valores realmente se fundem (nó único);
 *   - a profundidade (depth) confere com a contagem de passos.
 */
#include <stdio.h>
#include <stdlib.h>
#include "collatz_tree.h"
#include "collatz.h"

static int falhas = 0;

/* Segue os sucessores a partir de um nó e conta os passos até 1. */
static long walk_to_one(const CollatzTree *t, long start, int *ok)
{
    long steps = 0;
    long cur = start;
    *ok = 0;
    for (size_t guard = 0; guard < t->count + 5; guard++) {
        if (cur < 0 || (size_t)cur >= t->count)
            return -1;
        if (t->nodes[cur].value == 1) {
            *ok = 1;
            return steps;
        }
        long nx = t->nodes[cur].next;
        if (nx < 0)
            return -1;
        cur = nx;
        steps++;
    }
    return -1;
}

static long find_value(const CollatzTree *t, uint64_t v)
{
    for (size_t i = 0; i < t->count; i++)
        if (t->nodes[i].value == v)
            return (long)i;
    return -1;
}

int main(void)
{
    /* Caso 1: varias folhas que se fundem. */
    uint64_t sel[] = {27, 12, 7, 100};
    size_t nsel = sizeof(sel) / sizeof(sel[0]);

    CollatzTreeBuilder *b = collatz_tree_builder_new(sel, nsel);
    if (b == NULL) {
        printf("FALHA: builder nulo\n");
        return 1;
    }
    CollatzTree t;
    if (collatz_tree_build(b, &t) != 0) {
        printf("FALHA: build\n");
        return 1;
    }

    printf("Nos na arvore: %zu, raiz=%ld (valor %llu)\n", t.count,
           t.root_index, (unsigned long long)t.nodes[t.root_index].value);
    if (t.nodes[t.root_index].value != 1) {
        printf("  FALHA: raiz nao e 1\n");
        falhas++;
    }

    for (size_t i = 0; i < nsel; i++) {
        long idx = find_value(&t, sel[i]);
        if (idx < 0) {
            printf("  FALHA: folha %llu ausente\n",
                   (unsigned long long)sel[i]);
            falhas++;
            continue;
        }
        int ok = 0;
        long steps = walk_to_one(&t, idx, &ok);
        uint64_t real = collatz_steps(sel[i]);
        printf("  folha %llu: passos na arvore=%ld, esperado=%llu, chega em 1=%s\n",
               (unsigned long long)sel[i], steps,
               (unsigned long long)real, ok ? "sim" : "NAO");
        if (!ok) { printf("    FALHA: nao chega em 1\n"); falhas++; }
        if ((uint64_t)steps != real) {
            printf("    FALHA: passos divergem\n");
            falhas++;
        }
        if (t.nodes[idx].depth != (int)real) {
            printf("    FALHA: depth=%d esperado=%llu\n",
                   t.nodes[idx].depth, (unsigned long long)real);
            falhas++;
        }
    }

    /* Caso 2: fusao explicita. Caminhos do 16 e do 5 passam por 16->8->4->2->1. */
    uint64_t sel2[] = {16, 5};
    CollatzTreeBuilder *b2 = collatz_tree_builder_new(sel2, 2);
    CollatzTree t2;
    collatz_tree_build(b2, &t2);
    long n16 = find_value(&t2, 16);
    long n5 = find_value(&t2, 5);
    /* 16 -> 8; e 5 -> 16 (pois 3*5+1=16). Logo next(5) deve ser o no 16. */
    if (n5 >= 0 && n16 >= 0 && t2.nodes[n5].next != n16) {
        printf("  FALHA: esperava 5 -> 16 (fusao)\n");
        falhas++;
    } else {
        printf("  fusao 5 -> 16 OK\n");
    }

    collatz_tree_free(&t);
    collatz_tree_free(&t2);
    collatz_tree_builder_free(b);
    collatz_tree_builder_free(b2);

    /* Caso 3: layout. */
    uint64_t sel3[] = {27, 97, 871};
    CollatzTreeBuilder *b3 = collatz_tree_builder_new(sel3, 3);
    CollatzTree t3;
    collatz_tree_build(b3, &t3);
    CollatzLayout *lay = NULL;
    if (collatz_tree_layout(&t3, &lay, 120) == 0 && lay != NULL) {
        int bad = 0;
        for (size_t i = 0; i < t3.count; i++) {
            if (lay[i].x < 0.0 || lay[i].x > 1.0 ||
                lay[i].y < 0.0 || lay[i].y > 1.0)
                bad++;
        }
        printf("  layout: %zu posicoes, fora de [0,1]=%d, raiz em (%.2f,%.2f)\n",
               t3.count, bad, lay[t3.root_index].x, lay[t3.root_index].y);
        if (bad > 0) { printf("    FALHA: posicoes invalidas\n"); falhas++; }
        if (lay[t3.root_index].y != 1.0) {
            printf("    FALHA: raiz deveria estar na base\n"); falhas++;
        }
        free(lay);
    } else {
        printf("  FALHA: layout nulo\n");
        falhas++;
    }
    collatz_tree_free(&t3);
    collatz_tree_builder_free(b3);

    /* Caso 4: caminhos independentes (API usada pelo app macOS). */
    uint64_t sel4[] = {27, 97, 871};
    CollatzPaths paths;
    if (collatz_paths_build(sel4, 3, 160, &paths) == 0) {
        printf("  caminhos: %zu colunas, max_step=%d, wrapped=%d\n",
               paths.count, paths.max_step, paths.wrapped);
        if (paths.count != 3) { printf("    FALHA: esperava 3 colunas\n"); falhas++; }
        for (size_t p = 0; p < paths.count; p++) {
            CollatzPath *path = &paths.paths[p];
            int one_at_end = path->points[path->count - 1].value == 1;
            double y0 = path->points[0].y;
            double y1 = path->points[path->count - 1].y;
            uint64_t real = collatz_steps(path->start);
            printf("    coluna %zu: n=%llu pontos=%zu passo_final=1?%s "
                   "y=[%.2f..%.2f] (real=%llu)\n",
                   p, (unsigned long long)path->start, path->count,
                   one_at_end ? "sim" : "NAO", y0, y1,
                   (unsigned long long)real);
            if (!one_at_end) { printf("      FALHA: nao termina em 1\n"); falhas++; }
            if (path->count != real + 1) {
                printf("      FALHA: tamanho do caminho diverge\n"); falhas++;
            }
            if (y1 < 0.99) {
                printf("      FALHA: coluna deveria terminar na base (y~1)\n");
                falhas++;
            }
        }
        collatz_paths_free(&paths);
    } else {
        printf("  FALHA: collatz_paths_build\n");
        falhas++;
    }

    /* Caso 5: grafo com fusões (API usada pelas interfaces novas). */
    {
        uint64_t selg[] = {5461, 5460, 5456, 909, 908, 151};
        CollatzGraph g;
        if (collatz_graph_build(selg, 6, &g) == 0) {
            printf("  grafo: %zu nos, %zu arestas, max_depth=%d\n",
                   g.node_count, g.edge_count, g.max_depth);
            /* Deve haver o nó 1 e nós de tronco (fusões). */
            int has_one = 0, trunks = 0, leaves = 0;
            for (size_t i = 0; i < g.node_count; i++) {
                if (g.nodes[i].value == 1) has_one = 1;
                if (g.nodes[i].is_trunk) trunks++;
                if (g.nodes[i].is_leaf) leaves++;
            }
            printf("    tem 1=%s, troncos=%d, folhas=%d\n",
                   has_one ? "sim" : "NAO", trunks, leaves);
            if (!has_one) { printf("    FALHA: faltou o no 1\n"); falhas++; }
            if (trunks < 1) { printf("    FALHA: sem fusoes\n"); falhas++; }
            if (leaves != 6) { printf("    FALHA: folhas != 6\n"); falhas++; }

            /* Verifica uma fusão conhecida: 1024 recebe 2048 e 341. */
            for (size_t i = 0; i < g.node_count; i++) {
                if (g.nodes[i].value == 1024) {
                    int from2048 = 0, from341 = 0, indeg = 0;
                    for (size_t e = 0; e < g.edge_count; e++) {
                        if (g.edges[e].to != i) continue;
                        indeg++;
                        if (g.nodes[g.edges[e].from].value == 2048) from2048 = 1;
                        if (g.nodes[g.edges[e].from].value == 341) from341 = 1;
                    }
                    printf("    1024: entradas=%d (2048=%d, 341=%d)\n",
                           indeg, from2048, from341);
                    if (!from2048 || !from341) {
                        printf("    FALHA: fusao 1024 esperada de 2048 e 341\n");
                        falhas++;
                    }
                }
            }

            /* Todas as posições devem estar em [0,1]. */
            int bad = 0;
            for (size_t i = 0; i < g.node_count; i++)
                if (g.nodes[i].x < 0.0 || g.nodes[i].x > 1.0 ||
                    g.nodes[i].y < 0.0 || g.nodes[i].y > 1.0)
                    bad++;
            if (bad) { printf("    FALHA: %d posicoes fora de [0,1]\n", bad); falhas++; }
            /* O 1 deve estar na base (y=1). */
            if (g.root_index >= 0 && g.nodes[g.root_index].y < 0.99) {
                printf("    FALHA: o 1 deveria estar na base\n");
                falhas++;
            }
            collatz_graph_free(&g);
        } else {
            printf("  FALHA: collatz_graph_build\n");
            falhas++;
        }
    }

    printf(falhas ? "\n%d FALHA(S)\n" : "\nTodos os testes de arvore passaram.\n",
           falhas);
    return falhas ? 1 : 0;
}
