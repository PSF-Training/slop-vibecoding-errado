/*
 * main.c - Interface gráfica GTK3 do explorador da conjectura de Collatz.
 *
 * Três abas:
 *   1. Sequência  - gera a sequência de um número de partida e a
 *                   apresenta como um painel animado 2D/3D.
 *   2. Recordes   - varre uma faixa em busca da sequência mais longa
 *                   (paralelo, com progresso e botão cancelar).
 */
#define _POSIX_C_SOURCE 200809L
#include <gtk/gtk.h>
#include <errno.h>
#include <math.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "collatz.h"
#include "collatz_tree.h"

#define APP_ID "br.dev.collatz.explorador"

/* ------------------------------------------------------------------ */
/* Estado global da aplicação                                         */
/* ------------------------------------------------------------------ */

typedef struct {
    /* Aba Sequência */
    GtkWidget *entry_start;
    GtkWidget *viz;               /* painel animado (substitui o texto) */
    GtkWidget *chk_3d;            /* alterna 2D / 3D                   */
    GtkWidget *chk_auto_rotate;   /* giro contínuo                     */
    GtkWidget *lbl_stats;
    GtkWidget *lbl_status;

    /* Estado da animação */
    guint anim_tick;              /* id do timer                       */
    double clock;                 /* tempo desde o início, em segundos */
    double rot_yaw, rot_pitch;    /* ângulos atuais da câmera          */
    double target_yaw;            /* alvo de giro automático           */
    int anim_running;
    int hover_index;              /* nó sob o cursor, -1 = nenhum      */
    double drag_x, drag_y;        /* última posição do arrasto         */

    /* Aba Recordes */
    GtkWidget *entry_range_start;
    GtkWidget *entry_range_end;
    GtkWidget *spin_threads;
    GtkWidget *chk_limit_steps;
    GtkWidget *spin_max_steps;
    GtkWidget *lbl_record;
    GtkWidget *progress;
    GtkWidget *btn_search;
    GtkWidget *btn_cancel;

    /* Aba Árvore (grafo de Collatz com fusões) */
    GtkWidget *tree_entry;        /* campo com vários números          */
    GtkWidget *tree_viz;          /* area de desenho da árvore         */
    GtkWidget *tree_lbl_stats;
    CollatzGraph tree_graph;      /* grafo carregado                   */
    int tree_has_paths;
    double tree_clock;
    double tree_zoom;
    double tree_pan_x, tree_pan_y;
    int tree_hover_node;
    int tree_dragging;
    double tree_drag_x, tree_drag_y;
    guint tree_tick;

    /* Dados */
    uint64_t *seq;
    size_t seq_len;
    int seq_overflow;
    uint64_t seq_start;

    /* Estado da busca */
    volatile int search_canceled;
    volatile int search_running;
    pthread_t search_thread;
} App;

static App g_app;

/* Valor inicial passado por --demo N (0 = nenhum). */
static uint64_t g_startup_value = 0;

/* Se 1, abre já na aba Árvore (--tree). */
static int g_startup_tree = 0;

/* Números passados em --tree N M ... (aba Árvore). */
static char g_startup_tree_numbers[1024] = "";

/* Guarda o notebook e o índice da aba Árvore para seleção inicial. */
static GtkWidget *g_notebook = NULL;
static int g_tree_page = -1;

/* Comunicação thread de busca -> thread GTK. */
typedef struct {
    int status; /* 0 ok, 1 cancelado, -1 erro */
    CollatzRecord rec;
    uint64_t elapsed_ms;
} SearchResult;

static gboolean on_search_finished(gpointer data);
static void collatz_show_sequence(App *app, uint64_t start);

/* ------------------------------------------------------------------ */
/* Painel animado da sequência (2D/3D, estilo engine de jogos)        */
/* ------------------------------------------------------------------ */

/*
 * Visualização: cada valor da sequência vira um "nó" brilhante e as
 * transições viram setas coloridas. Os nós são distribuídos numa hélice:
 *   - eixo X: índice do passo (linear)
 *   - eixo Y: valor em escala logarítmica
 *   - eixo Z: oscilação suave, criando profundidade
 * A câmera tem rotação em Y (yaw) e leve inclinação (pitch). O giro
 * automático faz a hélice girar; o usuário pode arrastar para controlar.
 *
 * Cor de cada ligação pela paridade:
 *   - par  -> meia-noite no valor atual (ramo que divide por 2)   -> azul
 *   - ímpar -> 3n+1 (ramo que cresce)                              -> laranja
 */

typedef struct {
    double x, y, z; /* 3D */
} V3;

/* Matriz de projeção: aplica yaw/pitch e perspectiva simples. */
static V3 project_point(const V3 *p, double yaw, double pitch,
                        double cx, double cy, double scale)
{
    /* Rotação em Y (yaw). */
    double cyaw = cos(yaw), syaw = sin(yaw);
    double x1 = p->x * cyaw + p->z * syaw;
    double z1 = -p->x * syaw + p->z * cyaw;
    double y1 = p->y;

    /* Rotação em X (pitch). */
    double cpit = cos(pitch), spit = sin(pitch);
    double y2 = y1 * cpit - z1 * spit;
    double z2 = y1 * spit + z1 * cpit;

    /* Perspectiva. */
    const double d = 900.0;
    double f = d / (d + z2);
    V3 r;
    r.x = cx + x1 * scale * f;
    r.y = cy + y2 * scale * f;
    r.z = z2;
    return r;
}

/* Paleta da ligação: da cor fria (divide) à quente (cresce). */
static void edge_color(double t, double *r, double *g, double *b)
{
    /* t em [0,1]: 0 = azul/ciano, 1 = laranja/vermelho. */
    const double c0[3] = {0.20, 0.62, 1.00};
    const double c1[3] = {1.00, 0.45, 0.20};
    *r = c0[0] + (c1[0] - c0[0]) * t;
    *g = c0[1] + (c1[1] - c0[1]) * t;
    *b = c0[2] + (c1[2] - c0[2]) * t;
}

/* Desenha a sequência já com as transformações aplicadas. */
static void draw_sequence_view(cairo_t *cr, App *app, int width, int height)
{
    const int use3d = gtk_toggle_button_get_active(
        GTK_TOGGLE_BUTTON(app->chk_3d));

    /* Fundo com vinheta. */
    cairo_pattern_t *bg = cairo_pattern_create_radial(
        width * 0.5, height * 0.42, 40, width * 0.5, height * 0.5,
        (width > height ? width : height) * 0.75);
    cairo_pattern_add_color_stop_rgb(bg, 0, 0.09, 0.10, 0.15);
    cairo_pattern_add_color_stop_rgb(bg, 1, 0.03, 0.03, 0.06);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    if (app->seq_len == 0) {
        cairo_set_source_rgb(cr, 0.55, 0.58, 0.65);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15);
        const char *msg = "Digite um número e clique em Gerar para ver a animação.";
        cairo_text_extents_t te;
        cairo_text_extents(cr, msg, &te);
        cairo_move_to(cr, (width - te.width) / 2, height / 2);
        cairo_show_text(cr, msg);
        return;
    }

    /* Limita quantos nós desenhamos para manter tudo fluido. */
    size_t visible = app->seq_len;
    const size_t MAX_NODES = 160;
    if (visible > MAX_NODES)
        visible = MAX_NODES;

    /* Escala vertical por log do maior valor. */
    double max_v = 1.0;
    for (size_t i = 0; i < visible; i++)
        if ((double)app->seq[i] > max_v)
            max_v = (double)app->seq[i];
    double log_max = log(max_v);
    if (log_max <= 0)
        log_max = 1.0;

    /* Constrói as posições locais (hélice). */
    V3 *pts = g_malloc(visible * sizeof(V3));
    const double span_x = 900.0;
    for (size_t i = 0; i < visible; i++) {
        double t = (visible > 1) ? (double)i / (double)(visible - 1) : 0.0;
        double v = (double)app->seq[i];
        double ly = (log(v) > 0) ? log(v) : 0.0;
        pts[i].x = (t - 0.5) * span_x;
        pts[i].y = -(ly / log_max) * 380.0 + 190.0;
        /* A hélice: fase girando a cada passo, amplitude fixa. */
        pts[i].z = use3d ? sin(t * 16.0) * 130.0 : 0.0;
    }

    const double cx = width * 0.5;
    const double cy = height * 0.5 + 30.0;
    double scale = 0.85;
    if (width < 700)
        scale = width / 700.0;

    const double yaw = use3d ? app->rot_yaw : 0.0;
    const double pitch = use3d ? app->rot_pitch : 0.0;

    /* Projeta todos os pontos uma vez. */
    V3 *sp = g_malloc(visible * sizeof(V3));
    for (size_t i = 0; i < visible; i++)
        sp[i] = project_point(&pts[i], yaw, pitch, cx, cy, scale);

    /* Ordena as ligações por profundidade para um desenho crível. */
    size_t edges = visible > 0 ? visible - 1 : 0;
    size_t *order = g_malloc((edges ? edges : 1) * sizeof(size_t));
    for (size_t i = 0; i < edges; i++)
        order[i] = i;
    /* insertion sort por z médio (edges é pequeno). */
    for (size_t i = 1; i < edges; i++) {
        size_t key = order[i];
        double kz = (sp[key].z + sp[key + 1].z) * 0.5;
        size_t j = i;
        while (j > 0) {
            size_t prev = order[j - 1];
            double pz = (sp[prev].z + sp[prev + 1].z) * 0.5;
            if (pz > kz)
                break;
            order[j] = prev;
            j--;
        }
        order[j] = key;
    }

    /* Desenha as setas (ligações) com brilho. */
    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);
    for (size_t k = 0; k < edges; k++) {
        size_t i = order[k];
        int is_odd = (app->seq[i] & 1u); /* ímpar => 3n+1 (cresce) */
        double r, g, b;
        edge_color(is_odd ? 1.0 : 0.0, &r, &g, &b);

        /* Fade por profundidade (mais longe = mais escuro). */
        double depth = (sp[i].z + sp[i + 1].z) * 0.5;
        double dfac = 1.0 - (depth + 200.0) / 500.0;
        if (dfac < 0.25) dfac = 0.25;
        if (dfac > 1.0) dfac = 1.0;

        double x0 = sp[i].x, y0 = sp[i].y;
        double x1 = sp[i + 1].x, y1 = sp[i + 1].y;

        /* Glow: traço largo translúcido + traço fino. */
        cairo_set_source_rgba(cr, r, g, b, 0.16 * dfac);
        cairo_set_line_width(cr, 9.0);
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
        cairo_stroke(cr);

        cairo_set_source_rgba(cr, r, g, b, 0.9 * dfac);
        cairo_set_line_width(cr, 2.4);
        cairo_move_to(cr, x0, y0);
        cairo_line_to(cr, x1, y1);
        cairo_stroke(cr);

        /* Ponta da seta. */
        double dx = x1 - x0, dy = y1 - y0;
        double len = sqrt(dx * dx + dy * dy);
        if (len > 8.0) {
            double ux = dx / len, uy = dy / len;
            double hs = 9.0;
            double ax = x1 - ux * hs;
            double ay = y1 - uy * hs;
            double px = -uy, py = ux;
            cairo_set_source_rgba(cr, r, g, b, 0.9 * dfac);
            cairo_move_to(cr, x1, y1);
            cairo_line_to(cr, ax + px * hs * 0.5, ay + py * hs * 0.5);
            cairo_line_to(cr, ax - px * hs * 0.5, ay - py * hs * 0.5);
            cairo_close_path(cr);
            cairo_fill(cr);
        }
    }

    /* Desenha os nós (bolhas pulsantes). */
    const double pulse = 0.5 + 0.5 * sin(app->clock * 3.0);
    for (size_t i = 0; i < visible; i++) {
        double depth = sp[i].z;
        double dfac = 1.0 - (depth + 200.0) / 500.0;
        if (dfac < 0.3) dfac = 0.3;
        if (dfac > 1.0) dfac = 1.0;

        double rad = (4.0 + 2.0 * pulse) * dfac;
        if (i == 0)
            rad *= 1.5;                 /* nó inicial */
        if (i == visible - 1 && visible == app->seq_len)
            rad *= 1.4;                 /* chegada em 1 */

        /* Cor do nó: do azul ao verde conforme a altura. */
        double hgt = (double)(sp[i].y - (cy + 190.0 * scale)) / (380.0 * scale);
        double nn = (hgt > 1) ? 1 : (hgt < 0 ? 0 : hgt);
        double nr = 0.35 + 0.25 * nn, ng = 0.80 - 0.15 * nn, nb = 1.00 - 0.35 * nn;

        /* Halo. */
        cairo_set_source_rgba(cr, nr, ng, nb, 0.12 * dfac);
        cairo_arc(cr, sp[i].x, sp[i].y, rad * 3.2, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Núcleo. */
        cairo_pattern_t *grd = cairo_pattern_create_radial(
            sp[i].x - rad * 0.3, sp[i].y - rad * 0.3, 0,
            sp[i].x, sp[i].y, rad);
        cairo_pattern_add_color_stop_rgba(grd, 0, 1, 1, 1, 0.95 * dfac);
        cairo_pattern_add_color_stop_rgba(grd, 0.5, nr, ng, nb, 0.95 * dfac);
        cairo_pattern_add_color_stop_rgba(grd, 1, nr * 0.4, ng * 0.4, nb * 0.5,
                                          0.9 * dfac);
        cairo_set_source(cr, grd);
        cairo_arc(cr, sp[i].x, sp[i].y, rad, 0, 2 * G_PI);
        cairo_fill(cr);
        cairo_pattern_destroy(grd);

        /* Nó destacado (hover): anel. */
        if ((int)i == app->hover_index) {
            cairo_set_source_rgba(cr, 1.0, 1.0, 1.0, 0.9);
            cairo_set_line_width(cr, 2.0);
            cairo_arc(cr, sp[i].x, sp[i].y, rad + 5.0, 0, 2 * G_PI);
            cairo_stroke(cr);
        }
    }

    /* Rótulo do nó sob o cursor. */
    if (app->hover_index >= 0 && (size_t)app->hover_index < visible) {
        size_t i = (size_t)app->hover_index;
        char buf[96];
        snprintf(buf, sizeof buf, "n = %llu   •   passo %zu",
                 (unsigned long long)app->seq[i], i);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 13);
        cairo_text_extents_t te;
        cairo_text_extents(cr, buf, &te);
        double bx = sp[i].x + 12, by = sp[i].y - 30;
        if (bx + te.width + 16 > width)
            bx = width - te.width - 16;
        if (bx < 6)
            bx = 6;
        if (by < 6)
            by = 6;
        cairo_set_source_rgba(cr, 0.05, 0.06, 0.10, 0.85);
        cairo_rectangle(cr, bx - 6, by - 4, te.width + 12, te.height + 10);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.95, 0.97, 1.0);
        cairo_move_to(cr, bx, by + te.height);
        cairo_show_text(cr, buf);
    }

    /* Rodapé com legenda. */
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    cairo_set_source_rgb(cr, 0.55, 0.85, 1.0);
    cairo_rectangle(cr, 14, height - 26, 10, 10);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.80, 0.83, 0.90);
    cairo_move_to(cr, 30, height - 16);
    cairo_show_text(cr, "par (n/2)");
    cairo_set_source_rgb(cr, 1.0, 0.55, 0.30);
    cairo_rectangle(cr, 110, height - 26, 10, 10);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.80, 0.83, 0.90);
    cairo_move_to(cr, 126, height - 16);
    cairo_show_text(cr, "ímpar (3n+1)");

    char info[64];
    snprintf(info, sizeof info, "%zu de %zu nós", visible, app->seq_len);
    cairo_text_extents_t te;
    cairo_text_extents(cr, info, &te);
    cairo_move_to(cr, width - te.width - 14, height - 16);
    cairo_show_text(cr, info);

    g_free(pts);
    g_free(sp);
    g_free(order);
}

/* ------------------------------------------------------------------ */
/* Temporizador da animação                                           */
/* ------------------------------------------------------------------ */

static gboolean anim_timer(gpointer data)
{
    App *app = (App *)data;

    /* Avança o relógio para as pulsações. */
    app->clock += 0.033;

    if (gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_auto_rotate)) &&
        gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_3d))) {
        app->rot_yaw += 0.006;
        if (app->rot_yaw > 2 * G_PI)
            app->rot_yaw -= 2 * G_PI;
        /* Balanço suave no pitch. */
        app->rot_pitch = sin(app->clock * 0.4) * 0.18;
    }

    gtk_widget_queue_draw(app->viz);
    return G_SOURCE_CONTINUE;
}

static void ensure_anim_running(App *app)
{
    if (!app->anim_running) {
        app->anim_running = 1;
        app->anim_tick = g_timeout_add(33, anim_timer, app);
    }
}

/* ------------------------------------------------------------------ */
/* Interação: arrastar para girar + hover                             */
/* ------------------------------------------------------------------ */

static gboolean on_viz_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    App *app = (App *)data;
    GtkAllocation alloc;
    gtk_widget_get_allocation(widget, &alloc);
    draw_sequence_view(cr, app, alloc.width, alloc.height);

    if (app->anim_running)
        gtk_widget_queue_draw(widget);
    return TRUE;
}

static gboolean on_viz_button_press(GtkWidget *widget, GdkEventButton *ev,
                                    gpointer data)
{
    (void)widget;
    App *app = (App *)data;
    if (ev->button == 1) {
        /* Desliga o giro automático ao começar a arrastar. */
        gtk_toggle_button_set_active(
            GTK_TOGGLE_BUTTON(app->chk_auto_rotate), FALSE);
        app->target_yaw = app->rot_yaw;
        app->drag_x = ev->x;
        app->drag_y = ev->y;
    }
    return TRUE;
}

static gboolean on_viz_motion(GtkWidget *widget, GdkEventMotion *ev,
                              gpointer data)
{
    (void)widget;
    App *app = (App *)data;

    if (ev->state & GDK_BUTTON1_MASK) {
        double dx = ev->x - app->drag_x;
        double dy = ev->y - app->drag_y;
        app->rot_yaw += dx * 0.01;
        app->rot_pitch += dy * 0.006;
        if (app->rot_pitch > 0.9) app->rot_pitch = 0.9;
        if (app->rot_pitch < -0.9) app->rot_pitch = -0.9;
        app->drag_x = ev->x;
        app->drag_y = ev->y;
        gtk_widget_queue_draw(app->viz);
    }

    /* Recalcula qual nó está sob o cursor. */
    GtkAllocation alloc;
    gtk_widget_get_allocation(app->viz, &alloc);
    int width = alloc.width, height = alloc.height;
    int use3d = gtk_toggle_button_get_active(GTK_TOGGLE_BUTTON(app->chk_3d));
    int hover = -1;

    if (app->seq_len > 0) {
        size_t visible = app->seq_len > 160 ? 160 : app->seq_len;
        double max_v = 1.0;
        for (size_t i = 0; i < visible; i++)
            if ((double)app->seq[i] > max_v) max_v = (double)app->seq[i];
        double log_max = log(max_v) > 0 ? log(max_v) : 1.0;
        double scale = (width < 700) ? width / 700.0 : 0.85;
        double cx = width * 0.5, cy = height * 0.5 + 30.0;
        double yaw = use3d ? app->rot_yaw : 0.0;
        double pitch = use3d ? app->rot_pitch : 0.0;

        double best_d2 = 22.0 * 22.0;
        for (size_t i = 0; i < visible; i++) {
            double t = (visible > 1) ? (double)i / (double)(visible - 1) : 0.0;
            double v = (double)app->seq[i];
            double ly = log(v) > 0 ? log(v) : 0.0;
            V3 p;
            p.x = (t - 0.5) * 900.0;
            p.y = -(ly / log_max) * 380.0 + 190.0;
            p.z = use3d ? sin(t * 16.0) * 130.0 : 0.0;
            V3 s = project_point(&p, yaw, pitch, cx, cy, scale);
            double dx = s.x - ev->x, dy = s.y - ev->y;
            double d2 = dx * dx + dy * dy;
            if (d2 < best_d2) {
                best_d2 = d2;
                hover = (int)i;
            }
        }
    }

    if (hover != app->hover_index) {
        app->hover_index = hover;
        gtk_widget_queue_draw(app->viz);
    }
    return TRUE;
}

static void on_viz_leave(GtkWidget *widget, GdkEventCrossing *ev, gpointer data)
{
    (void)widget;
    (void)ev;
    App *app = (App *)data;
    if (app->hover_index != -1) {
        app->hover_index = -1;
        gtk_widget_queue_draw(app->viz);
    }
}

/* ------------------------------------------------------------------ */
/* Aba Árvore: grafo de Collatz com fusões                            */
/* ------------------------------------------------------------------ */

/*
 * Desenha a árvore de Collatz em camadas. Cada número escolhido é uma
 * folha no topo; os caminhos descem e, quando chegam ao mesmo valor,
 * passam a compartilhar o MESMO nó (fusão), formando o tronco até o 1.
 * Os caminhos digitados ficam destacados; o resto aparece mais apagado.
 * A animação revela a árvore de cima para baixo.
 */

static void tree_screen(const App *app, const GtkAllocation *a,
                        double nx, double ny, double *sx, double *sy)
{
    double uw = a->width - 2 * 70.0;
    double uh = a->height - 2 * 90.0;
    if (uw < 1.0) uw = 1.0;
    if (uh < 1.0) uh = 1.0;
    double cx = 70.0 + app->tree_pan_x;
    double cy = 90.0 + app->tree_pan_y;
    *sx = cx + nx * uw * app->tree_zoom;
    *sy = cy + ny * uh * app->tree_zoom;
}

static void on_tree_draw(GtkWidget *widget, cairo_t *cr, gpointer data)
{
    App *app = (App *)data;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);

    /* Fundo escuro com leve vinheta. */
    cairo_pattern_t *bg = cairo_pattern_create_radial(
        a.width * 0.5, a.height * 0.42, 40, a.width * 0.5, a.height * 0.5,
        (a.width > a.height ? a.width : a.height) * 0.75);
    cairo_pattern_add_color_stop_rgb(bg, 0, 0.09, 0.10, 0.15);
    cairo_pattern_add_color_stop_rgb(bg, 1, 0.03, 0.03, 0.06);
    cairo_set_source(cr, bg);
    cairo_paint(cr);
    cairo_pattern_destroy(bg);

    if (!app->tree_has_paths || app->tree_graph.node_count == 0) {
        cairo_set_source_rgb(cr, 0.55, 0.58, 0.65);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_NORMAL);
        cairo_set_font_size(cr, 15);
        const char *msg =
            "Digite um ou mais números e clique em Desenhar árvore.";
        cairo_text_extents_t te;
        cairo_text_extents(cr, msg, &te);
        cairo_move_to(cr, (a.width - te.width) / 2, a.height / 2);
        cairo_show_text(cr, msg);
        return;
    }

    CollatzGraph *g = &app->tree_graph;
    int max_depth = g->max_depth > 0 ? g->max_depth : 1;
    double reveal_speed = max_depth / 3.0;
    if (reveal_speed < 8.0) reveal_speed = 8.0;
    double reveal_depth = app->tree_clock * reveal_speed;

    cairo_set_line_cap(cr, CAIRO_LINE_CAP_ROUND);

    /* Arestas: fundo primeiro, destacadas por cima. */
    for (int pass = 0; pass < 2; pass++) {
        for (size_t e = 0; e < g->edge_count; e++) {
            CollatzGraphEdge *ed = &g->edges[e];
            if ((ed->highlighted ? 1 : 0) != pass)
                continue;
            CollatzGraphNode *from = &g->nodes[ed->from];
            double reveal = (double)(max_depth - from->depth);
            if (reveal > reveal_depth + 6.0)
                continue;
            if (reveal > reveal_depth && !ed->highlighted)
                continue;

            double ax, ay, bx, by;
            tree_screen(app, &a, from->x, from->y, &ax, &ay);
            tree_screen(app, &a, g->nodes[ed->to].x, g->nodes[ed->to].y,
                        &bx, &by);

            double r = ed->grew ? 1.0 : 0.22;
            double gg = ed->grew ? 0.51 : 0.76;
            double b = ed->grew ? 0.28 : 0.42;
            double alpha = ed->highlighted ? 0.95 : 0.30;

            if (ed->highlighted) {
                cairo_set_source_rgba(cr, r, gg, b, 0.14);
                cairo_set_line_width(cr, 6.0);
                cairo_move_to(cr, ax, ay);
                cairo_line_to(cr, bx, by);
                cairo_stroke(cr);
            }
            cairo_set_source_rgba(cr, r, gg, b, alpha);
            cairo_set_line_width(cr, ed->highlighted ? 2.2 : 1.2);
            cairo_move_to(cr, ax, ay);
            cairo_line_to(cr, bx, by);
            cairo_stroke(cr);

            /* Ponta da seta. */
            double dx = bx - ax, dy = by - ay;
            double len = sqrt(dx * dx + dy * dy);
            if (len > 10.0) {
                double ux = dx / len, uy = dy / len;
                double hs = ed->highlighted ? 8.0 : 6.0;
                double tx = bx - ux * hs, ty = by - uy * hs;
                double px = -uy, py = ux;
                cairo_set_source_rgba(cr, r, gg, b, alpha);
                cairo_move_to(cr, bx, by);
                cairo_line_to(cr, tx + px * hs * 0.45, ty + py * hs * 0.45);
                cairo_line_to(cr, tx - px * hs * 0.45, ty - py * hs * 0.45);
                cairo_close_path(cr);
                cairo_fill(cr);
            }
        }
    }

    /* Nós. */
    static const double pal[8][3] = {
        {1.00, 0.55, 0.25}, {0.44, 0.86, 0.55}, {0.55, 0.66, 1.00},
        {1.00, 0.45, 0.62}, {0.95, 0.82, 0.30}, {0.45, 0.90, 0.90},
        {0.75, 0.55, 0.95}, {0.98, 0.68, 0.40}
    };
    double pulse = 0.5 + 0.5 * sin(app->tree_clock * 3.0);

    for (size_t i = 0; i < g->node_count; i++) {
        CollatzGraphNode *nd = &g->nodes[i];
        double reveal = (double)(max_depth - nd->depth);
        if (reveal > reveal_depth + 6.0)
            continue;
        if (reveal > reveal_depth && !nd->highlighted)
            continue;

        double x, y;
        tree_screen(app, &a, nd->x, nd->y, &x, &y);
        int one = (nd->value == 1);
        int leaf = nd->is_leaf;
        int trunk = nd->is_trunk;

        double rad = 2.6;
        if (nd->highlighted) rad = 3.4;
        if (trunk) rad = 5.0;
        if (leaf) rad = 6.5;
        if (one) rad = 8.0;
        rad += 1.2 * pulse;

        double nr, ng, nb;
        if (one) {
            nr = 0.47; ng = 0.94; nb = 0.71;
        } else if (leaf) {
            if (nd->highlighted) {
                int k = nd->leaf_color < 0 ? 0 : nd->leaf_color % 8;
                nr = pal[k][0]; ng = pal[k][1]; nb = pal[k][2];
            } else {
                nr = ng = nb = 0.55;
            }
        } else if (trunk) {
            nr = 0.22; ng = 0.76; nb = 0.42;
        } else if (nd->highlighted) {
            nr = 0.56; ng = 0.77; nb = 1.00;
        } else {
            nr = 0.30; ng = 0.42; nb = 0.62;
        }

        /* Halo. */
        double ha = (nd->highlighted || leaf || trunk || one) ? 0.14 : 0.05;
        cairo_set_source_rgba(cr, nr, ng, nb, ha);
        cairo_arc(cr, x, y, rad * 2.4, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Núcleo. */
        cairo_set_source_rgba(cr, nr, ng, nb, 1.0);
        cairo_arc(cr, x, y, rad, 0, 2 * G_PI);
        cairo_fill(cr);

        /* Rótulo: folhas, troncos e o 1. */
        if (leaf || trunk || one) {
            char lab[24];
            snprintf(lab, sizeof lab, "%llu",
                     (unsigned long long)nd->value);
            cairo_set_source_rgb(cr, 0.96, 0.97, 1.0);
            cairo_select_font_face(cr, "Monospace", CAIRO_FONT_SLANT_NORMAL,
                                   CAIRO_FONT_WEIGHT_BOLD);
            cairo_set_font_size(cr, (leaf || one) ? 13 : 11);
            cairo_move_to(cr, x + rad + 4, y + 5);
            cairo_show_text(cr, lab);
        }
    }

    /* Tooltip do nó sob o cursor. */
    if (app->tree_hover_node >= 0 &&
        (size_t)app->tree_hover_node < g->node_count) {
        CollatzGraphNode *nd = &g->nodes[app->tree_hover_node];
        double x, y;
        tree_screen(app, &a, nd->x, nd->y, &x, &y);
        char info[96];
        snprintf(info, sizeof info, "n = %llu   •   %d passos até 1",
                 (unsigned long long)nd->value, nd->depth);
        cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                               CAIRO_FONT_WEIGHT_BOLD);
        cairo_set_font_size(cr, 13);
        cairo_text_extents_t te;
        cairo_text_extents(cr, info, &te);
        double bx = x + 12, by = y - 30;
        if (bx + te.width + 16 > a.width)
            bx = a.width - te.width - 16;
        if (bx < 6) bx = 6;
        if (by < 6) by = 6;
        cairo_set_source_rgba(cr, 0.05, 0.06, 0.10, 0.9);
        cairo_rectangle(cr, bx - 6, by - 4, te.width + 12, te.height + 10);
        cairo_fill(cr);
        cairo_set_source_rgb(cr, 0.95, 0.97, 1.0);
        cairo_move_to(cr, bx, by + te.height);
        cairo_show_text(cr, info);
    }

    /* Legenda. */
    cairo_select_font_face(cr, "Sans", CAIRO_FONT_SLANT_NORMAL,
                           CAIRO_FONT_WEIGHT_NORMAL);
    cairo_set_font_size(cr, 12);
    double ly = a.height - 16;
    cairo_set_source_rgb(cr, 1.0, 0.51, 0.28);
    cairo_rectangle(cr, 14, ly - 10, 10, 10);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.80, 0.83, 0.90);
    cairo_move_to(cr, 30, ly);
    cairo_show_text(cr, "3n+1 (cresce)");
    cairo_set_source_rgb(cr, 0.22, 0.76, 0.42);
    cairo_rectangle(cr, 160, ly - 10, 10, 10);
    cairo_fill(cr);
    cairo_set_source_rgb(cr, 0.80, 0.83, 0.90);
    cairo_move_to(cr, 176, ly);
    cairo_show_text(cr, "n/2 (cai)");

    size_t trunks = 0;
    for (size_t i = 0; i < g->node_count; i++)
        if (g->nodes[i].is_trunk) trunks++;
    char info2[96];
    snprintf(info2, sizeof info2, "%zu nós • %zu fusões • %d níveis",
             g->node_count, trunks, g->max_depth);
    cairo_text_extents_t te2;
    cairo_text_extents(cr, info2, &te2);
    cairo_move_to(cr, a.width - te2.width - 14, ly);
    cairo_show_text(cr, info2);
}

static gboolean on_tree_tick(gpointer data)
{
    App *app = (App *)data;
    app->tree_clock += 0.033;
    gtk_widget_queue_draw(app->tree_viz);
    return G_SOURCE_CONTINUE;
}

/* Constrói o grafo a partir do texto do campo. */
static void tree_build_from_text(App *app)
{
    const char *text = gtk_entry_get_text(GTK_ENTRY(app->tree_entry));

    uint64_t sel[1024];
    size_t n = 0;
    const char *p = text;
    while (*p != '\0' && n < 1024) {
        while (*p == ' ' || *p == ',' || *p == ';' || *p == '\t' ||
               *p == '\n' || *p == '\r')
            p++;
        if (*p == '\0')
            break;
        char *end = NULL;
        unsigned long long v = strtoull(p, &end, 10);
        if (end == p)
            break;
        if (v > 0)
            sel[n++] = (uint64_t)v;
        p = end;
    }

    if (n == 0) {
        gtk_label_set_text(GTK_LABEL(app->tree_lbl_stats),
                           "Digite um ou mais números válidos.");
        return;
    }

    if (app->tree_has_paths) {
        collatz_graph_free(&app->tree_graph);
        app->tree_has_paths = 0;
    }

    if (collatz_graph_build(sel, n, &app->tree_graph) == 0) {
        app->tree_has_paths = 1;
        app->tree_clock = 0.0;
        app->tree_zoom = 1.0;
        app->tree_pan_x = 0.0;
        app->tree_pan_y = 0.0;
        app->tree_hover_node = -1;

        size_t trunks = 0;
        for (size_t i = 0; i < app->tree_graph.node_count; i++)
            if (app->tree_graph.nodes[i].is_trunk) trunks++;
        char stats[160];
        snprintf(stats, sizeof stats,
                 "%zu número(s) • %zu nós • %zu fusões",
                 n, app->tree_graph.node_count, trunks);
        gtk_label_set_text(GTK_LABEL(app->tree_lbl_stats), stats);
    } else {
        gtk_label_set_text(GTK_LABEL(app->tree_lbl_stats),
                           "Não foi possível construir a árvore.");
    }
    gtk_widget_queue_draw(app->tree_viz);
}

static void on_tree_draw_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    tree_build_from_text((App *)data);
}

static gboolean on_tree_button_press(GtkWidget *widget, GdkEventButton *ev,
                                     gpointer data)
{
    (void)widget;
    App *app = (App *)data;
    if (ev->button == 1) {
        app->tree_dragging = 1;
        app->tree_drag_x = ev->x;
        app->tree_drag_y = ev->y;
    }
    return TRUE;
}

static gboolean on_tree_button_release(GtkWidget *widget, GdkEventButton *ev,
                                       gpointer data)
{
    (void)widget;
    (void)ev;
    ((App *)data)->tree_dragging = 0;
    return TRUE;
}

static gboolean on_tree_motion(GtkWidget *widget, GdkEventMotion *ev,
                               gpointer data)
{
    App *app = (App *)data;
    GtkAllocation a;
    gtk_widget_get_allocation(widget, &a);

    if (app->tree_dragging) {
        app->tree_pan_x += ev->x - app->tree_drag_x;
        app->tree_pan_y += ev->y - app->tree_drag_y;
        app->tree_drag_x = ev->x;
        app->tree_drag_y = ev->y;
        gtk_widget_queue_draw(widget);
    }

    if (!app->tree_has_paths)
        return TRUE;

    /* Procura o nó mais próximo do cursor. */
    double best = 16.0 * 16.0;
    int bn = -1;
    for (size_t i = 0; i < app->tree_graph.node_count; i++) {
        double x, y;
        tree_screen(app, &a, app->tree_graph.nodes[i].x,
                    app->tree_graph.nodes[i].y, &x, &y);
        double dx = x - ev->x, dy = y - ev->y;
        double d2 = dx * dx + dy * dy;
        if (d2 < best) {
            best = d2;
            bn = (int)i;
        }
    }
    if (bn != app->tree_hover_node) {
        app->tree_hover_node = bn;
        gtk_widget_queue_draw(widget);
    }
    return TRUE;
}

static gboolean on_tree_scroll(GtkWidget *widget, GdkEventScroll *ev,
                               gpointer data)
{
    (void)widget;
    App *app = (App *)data;
    if (ev->direction == GDK_SCROLL_UP)
        app->tree_zoom *= 1.1;
    else if (ev->direction == GDK_SCROLL_DOWN)
        app->tree_zoom /= 1.1;
    if (app->tree_zoom < 0.3) app->tree_zoom = 0.3;
    if (app->tree_zoom > 8.0) app->tree_zoom = 8.0;
    gtk_widget_queue_draw(app->tree_viz);
    return TRUE;
}

static gboolean on_tree_leave(GtkWidget *widget, GdkEventCrossing *ev,
                              gpointer data)
{
    (void)widget;
    (void)ev;
    App *app = (App *)data;
    if (app->tree_hover_node != -1) {
        app->tree_hover_node = -1;
        gtk_widget_queue_draw(app->tree_viz);
    }
    return TRUE;
}

/* ------------------------------------------------------------------ */
/* Aba Sequência                                                      */
/* ------------------------------------------------------------------ */

static gboolean parse_u64(const char *s, uint64_t *out)
{
    if (s == NULL || *s == '\0')
        return FALSE;
    char *end = NULL;
    errno = 0;
    unsigned long long v = strtoull(s, &end, 10);
    if (errno != 0 || end == s)
        return FALSE;
    while (*end == ' ' || *end == '\t')
        end++;
    if (*end != '\0')
        return FALSE;
    *out = (uint64_t)v;
    return TRUE;
}

static void on_generate(GtkWidget *widget, gpointer data)
{
    (void)widget;
    App *app = (App *)data;
    const char *text = gtk_entry_get_text(GTK_ENTRY(app->entry_start));

    uint64_t start;
    if (!parse_u64(text, &start) || start == 0) {
        gtk_label_set_text(GTK_LABEL(app->lbl_status),
                           "Digite um número inteiro maior ou igual a 1.");
        return;
    }

    collatz_show_sequence(app, start);
}

/* Gera a sequência para `start` e atualiza toda a interface. */
static void collatz_show_sequence(App *app, uint64_t start)
{
    size_t cap = 4096;
    uint64_t *buf = malloc(cap * sizeof(uint64_t));
    if (buf == NULL) {
        gtk_label_set_text(GTK_LABEL(app->lbl_status), "Sem memória.");
        return;
    }

    size_t len = 0;
    int ovf = 0;
    while (collatz_generate(start, buf, cap, &len, &ovf) != 0) {
        cap *= 2;
        uint64_t *nb = realloc(buf, cap * sizeof(uint64_t));
        if (nb == NULL) {
            free(buf);
            gtk_label_set_text(GTK_LABEL(app->lbl_status), "Sem memória.");
            return;
        }
        buf = nb;
    }

    free(app->seq);
    app->seq = buf;
    app->seq_len = len;
    app->seq_overflow = ovf;
    app->seq_start = start;
    app->hover_index = -1;

    /* Reinicia a animação a partir do zero. */
    app->clock = 0.0;
    app->rot_yaw = -0.35;
    app->rot_pitch = 0.12;
    ensure_anim_running(app);

    /* Estatísticas. */
    uint64_t max_v = app->seq[0];
    for (size_t i = 1; i < len; i++)
        if (app->seq[i] > max_v)
            max_v = app->seq[i];

    char stats[256];
    snprintf(stats, sizeof stats,
             "Número: %llu    •    Passos até 1: %zu    •    Maior valor: %llu",
             (unsigned long long)start, len - 1, (unsigned long long)max_v);
    gtk_label_set_text(GTK_LABEL(app->lbl_stats), stats);

    if (ovf)
        gtk_label_set_text(GTK_LABEL(app->lbl_status),
                           "Aviso: a sequência ultrapassou 64 bits; foi truncada "
                           "no ponto seguro.");
    else
        gtk_label_set_text(GTK_LABEL(app->lbl_status),
                           "Sequência gerada. Arraste no painel para girar a "
                           "visualização.");

    gtk_widget_queue_draw(app->viz);
}

/* ------------------------------------------------------------------ */
/* Thread da busca por recordes                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t start, end;
    unsigned threads;
    uint64_t max_steps;
    volatile int *canceled;
} SearchArgs;

static gboolean update_progress(gpointer data)
{
    double frac = *(double *)data;
    g_free(data);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(g_app.progress), frac);
    return FALSE;
}

static void on_progress_cb(double fraction, void *ud)
{
    (void)ud;
    /* progress() roda na thread de busca; transferimos para a thread GTK. */
    double *d = g_malloc(sizeof(double));
    *d = fraction;
    g_idle_add(update_progress, d);
}

static void *search_thread_main(void *arg)
{
    SearchArgs *a = (SearchArgs *)arg;
    SearchResult *res = g_malloc0(sizeof(SearchResult));
    const uint64_t t0 = (uint64_t)(g_get_monotonic_time() / 1000);

    res->status = collatz_search(a->start, a->end, a->threads, a->max_steps,
                                 0, &res->rec, on_progress_cb, NULL,
                                 a->canceled);
    res->elapsed_ms = (uint64_t)(g_get_monotonic_time() / 1000) - t0;

    free(a);
    g_idle_add(on_search_finished, res);
    return NULL;
}

static gboolean on_search_finished(gpointer data)
{
    SearchResult *res = (SearchResult *)data;
    App *app = &g_app;
    app->search_running = 0;

    gtk_widget_set_sensitive(app->btn_search, TRUE);
    gtk_widget_set_sensitive(app->btn_cancel, FALSE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 1.0);

    if (res->status == 0 && res->rec.n != 0) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "Recorde na faixa:  n = %llu  com  %llu passos  "
                 "(%.2f s)",
                 (unsigned long long)res->rec.n,
                 (unsigned long long)res->rec.steps,
                 res->elapsed_ms / 1000.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_record), msg);
    } else if (res->status == 1) {
        char msg[256];
        snprintf(msg, sizeof msg,
                 "Busca cancelada/limitada. Melhor até agora: n = %llu "
                 "(%llu passos) em %.2f s.",
                 (unsigned long long)res->rec.n,
                 (unsigned long long)res->rec.steps,
                 res->elapsed_ms / 1000.0);
        gtk_label_set_text(GTK_LABEL(app->lbl_record), msg);
    } else {
        gtk_label_set_text(GTK_LABEL(app->lbl_record), "Nenhum resultado.");
    }

    g_free(res);
    return FALSE;
}

static void on_search_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    App *app = (App *)data;
    if (app->search_running)
        return;

    uint64_t a, b;
    if (!parse_u64(gtk_entry_get_text(GTK_ENTRY(app->entry_range_start)), &a) ||
        !parse_u64(gtk_entry_get_text(GTK_ENTRY(app->entry_range_end)), &b) ||
        a == 0 || b < a) {
        gtk_label_set_text(GTK_LABEL(app->lbl_record),
                           "Faixa inválida: use início >= 1 e fim >= início.");
        return;
    }

    SearchArgs *args = calloc(1, sizeof(SearchArgs));
    args->start = a;
    args->end = b;
    args->threads = (unsigned)gtk_spin_button_get_value(
        GTK_SPIN_BUTTON(app->spin_threads));
    args->max_steps = gtk_toggle_button_get_active(
                          GTK_TOGGLE_BUTTON(app->chk_limit_steps))
                          ? (uint64_t)gtk_spin_button_get_value(
                                GTK_SPIN_BUTTON(app->spin_max_steps))
                          : 0;
    app->search_canceled = 0;
    args->canceled = &app->search_canceled;

    app->search_running = 1;
    gtk_widget_set_sensitive(app->btn_search, FALSE);
    gtk_widget_set_sensitive(app->btn_cancel, TRUE);
    gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(app->progress), 0.0);
    gtk_label_set_text(GTK_LABEL(app->lbl_record), "Calculando…");

    if (pthread_create(&app->search_thread, NULL, search_thread_main, args) != 0) {
        app->search_running = 0;
        free(args);
        gtk_widget_set_sensitive(app->btn_search, TRUE);
        gtk_widget_set_sensitive(app->btn_cancel, FALSE);
        gtk_label_set_text(GTK_LABEL(app->lbl_record),
                           "Não foi possível iniciar a busca.");
    }
}

static void on_cancel_clicked(GtkWidget *widget, gpointer data)
{
    (void)widget;
    App *app = (App *)data;
    app->search_canceled = 1;
    gtk_label_set_text(GTK_LABEL(app->lbl_record), "Cancelando…");
}

/* ------------------------------------------------------------------ */
/* Construção da interface                                            */
/* ------------------------------------------------------------------ */

static GtkWidget *build_sequence_tab(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl = gtk_label_new("Número de partida:");
    app->entry_start = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->entry_start), "27");
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->entry_start),
                                   "Ex.: 27, 97, 871…");
    GtkWidget *btn = gtk_button_new_with_label("Gerar sequência");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_generate), app);
    g_signal_connect(app->entry_start, "activate", G_CALLBACK(on_generate), app);

    gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->entry_start, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    app->lbl_stats = gtk_label_new("Informe um número e clique em Gerar.");
    gtk_widget_set_halign(app->lbl_stats, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(app->lbl_stats), TRUE);
    gtk_box_pack_start(GTK_BOX(box), app->lbl_stats, FALSE, FALSE, 0);

    /* Painel animado que substitui o antigo texto da sequência. */
    app->viz = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->viz, 640, 420);
    gtk_widget_set_can_focus(app->viz, TRUE);
    gtk_widget_add_events(app->viz,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_BUTTON1_MOTION_MASK |
                          GDK_SCROLL_MASK);
    g_signal_connect(app->viz, "draw", G_CALLBACK(on_viz_draw), app);
    g_signal_connect(app->viz, "button-press-event",
                     G_CALLBACK(on_viz_button_press), app);
    g_signal_connect(app->viz, "motion-notify-event",
                     G_CALLBACK(on_viz_motion), app);
    g_signal_connect(app->viz, "leave-notify-event",
                     G_CALLBACK(on_viz_leave), app);

    GtkWidget *frame = gtk_frame_new("Sequência animada em 3D");
    gtk_container_add(GTK_CONTAINER(frame), app->viz);
    gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);

    /* Controles da animação. */
    GtkWidget *ctrl = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 12);
    app->chk_3d = gtk_check_button_new_with_label("Modo 3D");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_3d), TRUE);
    app->chk_auto_rotate = gtk_check_button_new_with_label("Girar automaticamente");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_auto_rotate), TRUE);
    GtkWidget *hint = gtk_label_new("Arraste para girar • passe o mouse sobre um nó");
    gtk_widget_set_sensitive(hint, FALSE);
    gtk_box_pack_start(GTK_BOX(ctrl), app->chk_3d, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(ctrl), app->chk_auto_rotate, FALSE, FALSE, 0);
    gtk_box_pack_end(GTK_BOX(ctrl), hint, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), ctrl, FALSE, FALSE, 0);

    app->lbl_status = gtk_label_new("");
    gtk_widget_set_halign(app->lbl_status, GTK_ALIGN_START);
    gtk_box_pack_start(GTK_BOX(box), app->lbl_status, FALSE, FALSE, 0);

    return box;
}

static GtkWidget *build_tree_tab(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget *lbl = gtk_label_new("Números (separados por espaço ou vírgula):");
    app->tree_entry = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->tree_entry), "27, 97, 871, 41");
    gtk_entry_set_placeholder_text(GTK_ENTRY(app->tree_entry),
                                   "Ex.: 27, 97, 871, 41");
    GtkWidget *btn = gtk_button_new_with_label("Desenhar árvore");
    g_signal_connect(btn, "clicked", G_CALLBACK(on_tree_draw_clicked), app);
    g_signal_connect(app->tree_entry, "activate",
                     G_CALLBACK(on_tree_draw_clicked), app);

    gtk_box_pack_start(GTK_BOX(row), lbl, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->tree_entry, TRUE, TRUE, 0);
    gtk_box_pack_start(GTK_BOX(row), btn, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    app->tree_lbl_stats = gtk_label_new(
        "Cada número vira uma folha; quando os caminhos se encontram, "
        "compartilham o mesmo nó (fusão) até o 1.");
    gtk_widget_set_halign(app->tree_lbl_stats, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(app->tree_lbl_stats), TRUE);
    gtk_box_pack_start(GTK_BOX(box), app->tree_lbl_stats, FALSE, FALSE, 0);

    app->tree_viz = gtk_drawing_area_new();
    gtk_widget_set_size_request(app->tree_viz, 640, 420);
    gtk_widget_add_events(app->tree_viz,
                          GDK_POINTER_MOTION_MASK |
                          GDK_BUTTON_PRESS_MASK |
                          GDK_BUTTON_RELEASE_MASK |
                          GDK_LEAVE_NOTIFY_MASK |
                          GDK_BUTTON1_MOTION_MASK |
                          GDK_SCROLL_MASK);
    g_signal_connect(app->tree_viz, "draw", G_CALLBACK(on_tree_draw), app);
    g_signal_connect(app->tree_viz, "button-press-event",
                     G_CALLBACK(on_tree_button_press), app);
    g_signal_connect(app->tree_viz, "button-release-event",
                     G_CALLBACK(on_tree_button_release), app);
    g_signal_connect(app->tree_viz, "motion-notify-event",
                     G_CALLBACK(on_tree_motion), app);
    g_signal_connect(app->tree_viz, "scroll-event",
                     G_CALLBACK(on_tree_scroll), app);
    g_signal_connect(app->tree_viz, "leave-notify-event",
                     G_CALLBACK(on_tree_leave), app);

    GtkWidget *frame = gtk_frame_new(
        "Árvore de Collatz com fusões (folhas no topo, 1 na base)");
    gtk_container_add(GTK_CONTAINER(frame), app->tree_viz);
    gtk_box_pack_start(GTK_BOX(box), frame, TRUE, TRUE, 0);

    GtkWidget *hint = gtk_label_new(
        "Arraste para mover • role para dar zoom • passe o mouse sobre um nó");
    gtk_widget_set_halign(hint, GTK_ALIGN_START);
    gtk_widget_set_sensitive(hint, FALSE);
    gtk_box_pack_start(GTK_BOX(box), hint, FALSE, FALSE, 0);

    /* Timer próprio da animação da árvore. */
    app->tree_clock = 0.0;
    app->tree_zoom = 1.0;
    app->tree_pan_x = 0.0;
    app->tree_pan_y = 0.0;
    app->tree_hover_node = -1;
    app->tree_tick = g_timeout_add(33, on_tree_tick, app);

    return box;
}

static GtkWidget *build_records_tab(App *app)
{
    GtkWidget *box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_container_set_border_width(GTK_CONTAINER(box), 12);

    GtkWidget *grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);

    app->entry_range_start = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->entry_range_start), "1");
    app->entry_range_end = gtk_entry_new();
    gtk_entry_set_text(GTK_ENTRY(app->entry_range_end), "1000000");

    app->spin_threads = gtk_spin_button_new_with_range(
        1, 256, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_threads),
                              collatz_cpu_count());

    app->chk_limit_steps = gtk_check_button_new_with_label(
        "Limitar passos por número");
    gtk_toggle_button_set_active(GTK_TOGGLE_BUTTON(app->chk_limit_steps), TRUE);
    app->spin_max_steps = gtk_spin_button_new_with_range(
        100, 1e12, 100);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(app->spin_max_steps), 100000);

    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Início:"), 0, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->entry_range_start, 1, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Fim:"), 2, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->entry_range_end, 3, 0, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), gtk_label_new("Threads:"), 0, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->spin_threads, 1, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->chk_limit_steps, 2, 1, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), app->spin_max_steps, 3, 1, 1, 1);
    gtk_box_pack_start(GTK_BOX(box), grid, FALSE, FALSE, 0);

    GtkWidget *row = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    app->btn_search = gtk_button_new_with_label("Buscar recorde");
    app->btn_cancel = gtk_button_new_with_label("Cancelar");
    gtk_widget_set_sensitive(app->btn_cancel, FALSE);
    g_signal_connect(app->btn_search, "clicked", G_CALLBACK(on_search_clicked), app);
    g_signal_connect(app->btn_cancel, "clicked", G_CALLBACK(on_cancel_clicked), app);
    gtk_box_pack_start(GTK_BOX(row), app->btn_search, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(row), app->btn_cancel, FALSE, FALSE, 0);
    gtk_box_pack_start(GTK_BOX(box), row, FALSE, FALSE, 0);

    app->progress = gtk_progress_bar_new();
    gtk_progress_bar_set_show_text(GTK_PROGRESS_BAR(app->progress), TRUE);
    gtk_box_pack_start(GTK_BOX(box), app->progress, FALSE, FALSE, 0);

    app->lbl_record = gtk_label_new("Informe a faixa e clique em Buscar recorde.");
    gtk_widget_set_halign(app->lbl_record, GTK_ALIGN_START);
    gtk_label_set_selectable(GTK_LABEL(app->lbl_record), TRUE);
    gtk_box_pack_start(GTK_BOX(box), app->lbl_record, FALSE, FALSE, 0);

    return box;
}

static void on_activate(GtkApplication *gtkapp, gpointer user_data)
{
    (void)user_data;
    App *app = &g_app;

    GtkWidget *window = gtk_application_window_new(gtkapp);
    gtk_window_set_title(GTK_WINDOW(window), "Explorador de Collatz");
    gtk_window_set_default_size(GTK_WINDOW(window), 760, 560);
    gtk_window_set_position(GTK_WINDOW(window), GTK_WIN_POS_CENTER);

    /* Ícone da janela (se o arquivo existir ao lado do executável). */
    GError *err = NULL;
    const char *candidates[] = {
        "assets/collatz.svg", "../assets/collatz.svg",
        "/usr/local/share/icons/collatz.svg", NULL};

    static char icon_path[512];
    int got_icon = 0;
    for (int i = 0; candidates[i] != NULL; i++) {
        if (g_file_test(candidates[i], G_FILE_TEST_EXISTS)) {
            if (gtk_window_set_icon_from_file(GTK_WINDOW(window),
                                              candidates[i], &err)) {
                g_strlcpy(icon_path, candidates[i], sizeof icon_path);
                got_icon = 1;
            } else if (err != NULL) {
                g_error_free(err);
                err = NULL;
            }
            if (got_icon)
                break;
        }
    }
    if (got_icon)
        gtk_window_set_default_icon_name("collatz");

    GtkWidget *notebook = gtk_notebook_new();
    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), build_sequence_tab(app),
        gtk_label_new("Sequência"));
    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), build_tree_tab(app),
        gtk_label_new("Árvore"));
    gtk_notebook_append_page(
        GTK_NOTEBOOK(notebook), build_records_tab(app),
        gtk_label_new("Recordes"));

    g_notebook = notebook;
    g_tree_page = 1;

    gtk_container_add(GTK_CONTAINER(window), notebook);
    gtk_widget_show_all(window);

    /* Inicia o relógio da animação. */
    ensure_anim_running(app);

    /* Se veio --demo N, já gera a sequência desse número. */
    if (g_startup_value != 0 && !g_startup_tree) {
        char tmp[24];
        snprintf(tmp, sizeof tmp, "%llu",
                 (unsigned long long)g_startup_value);
        gtk_entry_set_text(GTK_ENTRY(app->entry_start), tmp);
        collatz_show_sequence(app, g_startup_value);
    }

    /* Se veio --tree, abre na aba Árvore e desenha. */
    if (g_startup_tree) {
        if (g_startup_tree_numbers[0] != '\0') {
            gtk_entry_set_text(GTK_ENTRY(app->tree_entry),
                               g_startup_tree_numbers);
        } else if (g_startup_value != 0) {
            char tmp[24];
            snprintf(tmp, sizeof tmp, "%llu",
                     (unsigned long long)g_startup_value);
            gtk_entry_set_text(GTK_ENTRY(app->tree_entry), tmp);
        }
        gtk_notebook_set_current_page(GTK_NOTEBOOK(g_notebook), g_tree_page);
        tree_build_from_text(app);
    }
}

int main(int argc, char **argv)
{
    /* Aceita um número solto ou --demo N como valor inicial.
     * Removemos esses argumentos antes de entregar ao GTK, que não os
     * conhece. */
    char **gtk_argv = g_new0(char *, argc + 1);
    int gtk_argc = 0;
    gtk_argv[gtk_argc++] = argv[0];

    for (int i = 1; i < argc; i++) {
        if (g_strcmp0(argv[i], "--demo") == 0 && i + 1 < argc) {
            g_startup_value = strtoull(argv[i + 1], NULL, 10);
            i++;
        } else if (g_strcmp0(argv[i], "--tree") == 0) {
            /* --tree N M ...: abre a aba Árvore com vários números. */
            g_startup_tree = 1;
            while (i + 1 < argc && argv[i + 1][0] != '-') {
                i++;
                if (g_startup_tree_numbers[0] != '\0')
                    g_strlcat(g_startup_tree_numbers, " ",
                              sizeof g_startup_tree_numbers);
                g_strlcat(g_startup_tree_numbers, argv[i],
                          sizeof g_startup_tree_numbers);
            }
            if (g_startup_tree_numbers[0] != '\0')
                g_startup_value = strtoull(g_startup_tree_numbers, NULL, 10);
        } else if (argv[i][0] != '-') {
            uint64_t v;
            if (parse_u64(argv[i], &v) && v != 0)
                g_startup_value = v;
        } else {
            gtk_argv[gtk_argc++] = argv[i];
        }
    }

    GtkApplication *gtkapp = gtk_application_new(
        APP_ID, G_APPLICATION_DEFAULT_FLAGS);
    g_signal_connect(gtkapp, "activate", G_CALLBACK(on_activate), NULL);
    int status = g_application_run(G_APPLICATION(gtkapp), gtk_argc, gtk_argv);
    g_object_unref(gtkapp);
    g_free(gtk_argv);
    free(g_app.seq);
    if (g_app.tree_has_paths)
        collatz_graph_free(&g_app.tree_graph);
    return status;
}
