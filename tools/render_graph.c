/*
 * render_graph.c - Pré-visualização da árvore de Collatz COM FUSÕES.
 *
 * Usa exatamente collatz_graph_build(), a mesma função que as interfaces
 * usam, e escreve um SVG. Serve para conferir o desenho no Linux antes de
 * compilar no Mac. Não faz parte do app.
 *
 * Uso: render_graph 27 97 871 41 > graph.svg
 */
#include <stdio.h>
#include <stdlib.h>
#include "collatz_tree.h"

int main(int argc, char **argv)
{
    if (argc < 2) {
        fprintf(stderr, "uso: %s N [N...]\n", argv[0]);
        return 1;
    }
    size_t n = (size_t)(argc - 1);
    uint64_t *sel = malloc(n * sizeof(uint64_t));
    for (size_t i = 0; i < n; i++)
        sel[i] = strtoull(argv[i + 1], NULL, 10);

    CollatzGraph g;
    if (collatz_graph_build(sel, n, &g) != 0) {
        fprintf(stderr, "falha ao construir o grafo\n");
        return 1;
    }

    const double W = 1400, H = 900;
    const double MX = 60, MY = 60;
    double uw = W - 2 * MX, uh = H - 2 * MY;

    printf("<svg xmlns='http://www.w3.org/2000/svg' width='%g' height='%g' "
           "viewBox='0 0 %g %g'>\n", W, H, W, H);
    printf("<rect width='%g' height='%g' fill='#0e1020'/>\n", W, H);
    printf("<defs>"
           "<marker id='arrow-o' viewBox='0 0 10 10' refX='9' refY='5' "
           "markerWidth='6' markerHeight='6' orient='auto-start-reverse'>"
           "<path d='M0,0 L10,5 L0,10 z' fill='#ff823f'/></marker>"
           "<marker id='arrow-g' viewBox='0 0 10 10' refX='9' refY='5' "
           "markerWidth='6' markerHeight='6' orient='auto-start-reverse'>"
           "<path d='M0,0 L10,5 L0,10 z' fill='#37c26a'/></marker>"
           "</defs>\n");

    /* Arestas (as destacadas por último, para ficarem por cima). */
    for (int pass = 0; pass < 2; pass++) {
        for (size_t e = 0; e < g.edge_count; e++) {
            CollatzGraphEdge *ed = &g.edges[e];
            if ((ed->highlighted ? 1 : 0) != pass)
                continue;
            CollatzGraphNode *a = &g.nodes[ed->from];
            CollatzGraphNode *b = &g.nodes[ed->to];
            double x0 = MX + a->x * uw, y0 = MY + a->y * uh;
            double x1 = MX + b->x * uw, y1 = MY + b->y * uh;
            const char *col = ed->grew ? "#ff823f" : "#37c26a";
            printf("<line x1='%g' y1='%g' x2='%g' y2='%g' stroke='%s' "
                   "stroke-width='%g' stroke-opacity='%g' "
                   "marker-end='url(#arrow-%s)'/>\n",
                   x0, y0, x1, y1, col,
                   ed->highlighted ? 2.6 : 1.4,
                   ed->highlighted ? 1.0 : 0.45,
                   ed->grew ? "o" : "g");
        }
    }

    /* Nós. */
    for (size_t i = 0; i < g.node_count; i++) {
        CollatzGraphNode *nd = &g.nodes[i];
        double x = MX + nd->x * uw, y = MY + nd->y * uh;
        const char *fill = "#64a0e6";
        double r = 2.4;
        if (nd->value == 1) { fill = "#78f0b4"; r = 6; }
        else if (nd->is_leaf) { fill = nd->highlighted ? "#ff8c3c" : "#8a94b0"; r = 7; }
        else if (nd->is_trunk) { fill = "#37c26a"; r = 6; }
        else if (nd->highlighted) { fill = "#8fc4ff"; r = 3.2; }
        printf("<circle cx='%g' cy='%g' r='%g' fill='%s'/>\n", x, y, r, fill);
        if (nd->is_leaf || nd->is_trunk || nd->value == 1)
            printf("<text x='%g' y='%g' fill='#e8eeff' font-size='11' "
                   "font-family='Menlo,monospace' text-anchor='middle'>%llu</text>\n",
                   x, y - r - 3, (unsigned long long)nd->value);
    }
    printf("</svg>\n");

    collatz_graph_free(&g);
    free(sel);
    return 0;
}
