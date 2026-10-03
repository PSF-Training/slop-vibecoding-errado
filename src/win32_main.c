/*
 * win32_main.c - Interface Win32/GDI do Explorador de Collatz.
 *
 * Está para a versão Windows assim como main.c está para o GTK3: reusa
 * integralmente o motor em collatz.c e desenha a animação com GDI puro.
 *
 * Controles:
 *   - Campo de número + botão "Gerar sequencia"
 *   - Painel animado 2D/3D (mesma hélice do GTK)
 *   - Checkboxes "Modo 3D" e "Girar automaticamente"
 *   - Campo inicial/final + botão "Buscar recorde" e barra de progresso
 */
#define _WIN32_WINNT 0x0600
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <windowsx.h>
#include <commctrl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "collatz.h"
#include "collatz_tree.h"

/* IDs dos controles. */
#define ID_ENTRY_START   1001
#define ID_BTN_GENERATE  1002
#define ID_CHK_3D        1003
#define ID_CHK_ROTATE    1004
#define ID_ENTRY_RSTART  1005
#define ID_ENTRY_REND    1006
#define ID_BTN_SEARCH    1007
#define ID_BTN_CANCEL    1008
#define ID_PROGRESS      1009
#define ID_LBL_STATS     1010
#define ID_LBL_RECORD    1011
#define ID_RADIO_SEQ     1012
#define ID_RADIO_TREE    1013
#define ID_ENTRY_TREES   1014
#define ID_BTN_TREEDRAW  1015
#define ID_TIMER_ANIM    2001

/* ------------------------------------------------------------------ */
/* Estado global                                                      */
/* ------------------------------------------------------------------ */

static HWND g_hwnd;
static HWND g_entry_start, g_btn_generate, g_chk_3d, g_chk_rotate;
static HWND g_entry_rstart, g_entry_rend, g_btn_search, g_btn_cancel;
static HWND g_progress, g_lbl_stats, g_lbl_record;

/* Modo árvore. */
static HWND g_radio_seq, g_radio_tree, g_entry_trees, g_btn_treedraw;
static HWND g_lbl_treeNums;
static int g_show_tree = 0;             /* 0 = sequencia, 1 = arvore */
static CollatzGraph g_tree_graph;
static int g_tree_has_paths = 0;
static double g_tree_clock = 0.0;
static HCURSOR g_cursor_default;

static uint64_t *g_seq = NULL;
static size_t g_seq_len = 0;
static int g_seq_overflow = 0;
static uint64_t g_seq_start = 0;

static double g_clock = 0.0;
static double g_rot_yaw = -0.35;
static double g_rot_pitch = 0.12;
static int g_dragging = 0;
static int g_drag_x = 0, g_drag_y = 0;
static int g_hover_index = -1;

static volatile LONG g_search_running = 0;
static volatile LONG g_search_cancel = 0;
static CollatzRecord g_pending_record;
static volatile LONG g_search_done = 0;
static volatile LONG g_search_status = 0;
static double g_progress_fraction = 0.0;

/* ------------------------------------------------------------------ */
/* Utilidades                                                         */
/* ------------------------------------------------------------------ */

static int parse_u64(const char *s, uint64_t *out)
{
    if (s == NULL || *s == '\0')
        return 0;
    char *end = NULL;
    unsigned long long v = strtoull(s, &end, 10);
    if (end == s)
        return 0;
    while (*end == ' ' || *end == '\t')
        end++;
    if (*end != '\0')
        return 0;
    *out = (uint64_t)v;
    return 1;
}

static void generate_for(HWND hwnd, uint64_t start);

/* ------------------------------------------------------------------ */
/* Animação (mesma matemática da versão GTK)                          */
/* ------------------------------------------------------------------ */

typedef struct { double x, y, z; } V3;

static V3 project_point(const V3 *p, double yaw, double pitch,
                        double cx, double cy, double scale)
{
    double cyaw = cos(yaw), syaw = sin(yaw);
    double x1 = p->x * cyaw + p->z * syaw;
    double z1 = -p->x * syaw + p->z * cyaw;
    double y1 = p->y;
    double cpit = cos(pitch), spit = sin(pitch);
    double y2 = y1 * cpit - z1 * spit;
    double z2 = y1 * spit + z1 * cpit;
    const double d = 900.0;
    double f = d / (d + z2);
    V3 r;
    r.x = cx + x1 * scale * f;
    r.y = cy + y2 * scale * f;
    r.z = z2;
    return r;
}

static void edge_color(double t, double *r, double *g, double *b)
{
    const double c0[3] = {0.20, 0.62, 1.00};
    const double c1[3] = {1.00, 0.45, 0.20};
    *r = c0[0] + (c1[0] - c0[0]) * t;
    *g = c0[1] + (c1[1] - c0[1]) * t;
    *b = c0[2] + (c1[2] - c0[2]) * t;
}

#define MAX_NODES 160

static void draw_sequence(HDC dc, RECT *area)
{
    int width = area->right - area->left;
    int height = area->bottom - area->top;
    int use3d = (SendMessage(g_chk_3d, BM_GETCHECK, 0, 0) == BST_CHECKED);

    /* Fundo escuro. */
    HBRUSH bg = CreateSolidBrush(RGB(14, 16, 26));
    FillRect(dc, area, bg);
    DeleteObject(bg);

    /* Recorta para a área do painel. */
    HRGN clip = CreateRectRgn(area->left, area->top, area->right, area->bottom);
    SelectClipRgn(dc, clip);

    if (g_seq_len == 0) {
        SetBkMode(dc, TRANSPARENT);
        SetTextColor(dc, RGB(140, 148, 168));
        const char *msg =
            "Digite um numero e clique em Gerar para ver a animacao.";
        TextOutA(dc, area->left + 30, area->top + height / 2, msg,
                 (int)strlen(msg));
        SelectClipRgn(dc, NULL);
        DeleteObject(clip);
        return;
    }

    size_t visible = g_seq_len > MAX_NODES ? MAX_NODES : g_seq_len;

    double max_v = 1.0;
    for (size_t i = 0; i < visible; i++)
        if ((double)g_seq[i] > max_v)
            max_v = (double)g_seq[i];
    double log_max = log(max_v);
    if (log_max <= 0) log_max = 1.0;

    V3 *pts = (V3 *)malloc(visible * sizeof(V3));
    V3 *sp = (V3 *)malloc(visible * sizeof(V3));
    if (pts == NULL || sp == NULL) {
        free(pts); free(sp);
        SelectClipRgn(dc, NULL); DeleteObject(clip);
        return;
    }

    for (size_t i = 0; i < visible; i++) {
        double t = (visible > 1) ? (double)i / (double)(visible - 1) : 0.0;
        double v = (double)g_seq[i];
        double ly = log(v) > 0 ? log(v) : 0.0;
        pts[i].x = (t - 0.5) * 900.0;
        pts[i].y = -(ly / log_max) * 380.0 + 190.0;
        pts[i].z = use3d ? sin(t * 16.0) * 130.0 : 0.0;
    }

    double scale = (width < 700) ? width / 700.0 : 0.85;
    double cx = area->left + width * 0.5;
    double cy = area->top + height * 0.5 + 20.0;
    double yaw = use3d ? g_rot_yaw : 0.0;
    double pitch = use3d ? g_rot_pitch : 0.0;

    for (size_t i = 0; i < visible; i++)
        sp[i] = project_point(&pts[i], yaw, pitch, cx, cy, scale);

    /* Desenha as ligações (em ordem de profundidade, mais longe primeiro). */
    size_t edges = visible ? visible - 1 : 0;
    size_t *order = (size_t *)malloc((edges ? edges : 1) * sizeof(size_t));
    for (size_t i = 0; i < edges; i++) order[i] = i;
    for (size_t i = 1; i < edges; i++) {
        size_t key = order[i];
        double kz = (sp[key].z + sp[key + 1].z) * 0.5;
        size_t j = i;
        while (j > 0) {
            size_t prev = order[j - 1];
            double pz = (sp[prev].z + sp[prev + 1].z) * 0.5;
            if (pz > kz) break;
            order[j] = prev; j--;
        }
        order[j] = key;
    }

    HPEN pen_glow = CreatePen(PS_SOLID, 6, RGB(0, 0, 0)); /* cor real via brush */
    (void)pen_glow;

    for (size_t k = 0; k < edges; k++) {
        size_t i = order[k];
        int is_odd = (int)(g_seq[i] & 1u);
        double r, g, b;
        edge_color(is_odd ? 1.0 : 0.0, &r, &g, &b);
        double depth = (sp[i].z + sp[i + 1].z) * 0.5;
        double dfac = 1.0 - (depth + 200.0) / 500.0;
        if (dfac < 0.25) dfac = 0.25;
        if (dfac > 1.0) dfac = 1.0;

        COLORREF col = RGB((int)(r * dfac * 255), (int)(g * dfac * 255),
                           (int)(b * dfac * 255));
        HPEN pen = CreatePen(PS_SOLID, 2, col);
        HGDIOBJ old = SelectObject(dc, pen);
        MoveToEx(dc, (int)sp[i].x, (int)sp[i].y, NULL);
        LineTo(dc, (int)sp[i + 1].x, (int)sp[i + 1].y);

        /* Ponta da seta como pequeno triângulo. */
        double dx = sp[i + 1].x - sp[i].x;
        double dy = sp[i + 1].y - sp[i].y;
        double len = sqrt(dx * dx + dy * dy);
        if (len > 8.0) {
            double ux = dx / len, uy = dy / len;
            double hs = 7.0;
            double ax = sp[i + 1].x - ux * hs;
            double ay = sp[i + 1].y - uy * hs;
            double px = -uy, py = ux;
            POINT tri[3];
            tri[0].x = (LONG)sp[i + 1].x; tri[0].y = (LONG)sp[i + 1].y;
            tri[1].x = (LONG)(ax + px * hs * 0.5); tri[1].y = (LONG)(ay + py * hs * 0.5);
            tri[2].x = (LONG)(ax - px * hs * 0.5); tri[2].y = (LONG)(ay - py * hs * 0.5);
            HBRUSH br = CreateSolidBrush(col);
            SelectObject(dc, br);
            Polygon(dc, tri, 3);
            DeleteObject(br);
        }

        SelectObject(dc, old);
        DeleteObject(pen);
    }

    /* Nós. */
    double pulse = 0.5 + 0.5 * sin(g_clock * 3.0);
    for (size_t i = 0; i < visible; i++) {
        double depth = sp[i].z;
        double dfac = 1.0 - (depth + 200.0) / 500.0;
        if (dfac < 0.3) dfac = 0.3;
        if (dfac > 1.0) dfac = 1.0;
        double rad = (4.0 + 2.0 * pulse) * dfac;
        if (i == 0) rad *= 1.5;

        double hgt = (sp[i].y - (cy - 190.0 * scale)) / (380.0 * scale);
        double nn = hgt > 1 ? 1 : (hgt < 0 ? 0 : hgt);
        double nr = 0.35 + 0.25 * nn, ng = 0.80 - 0.15 * nn, nb = 1.00 - 0.35 * nn;
        COLORREF core = RGB((int)(nr * 255), (int)(ng * 255), (int)(nb * 255));

        HBRUSH br = CreateSolidBrush(core);
        HGDIOBJ ob = SelectObject(dc, br);
        HPEN op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, (int)(sp[i].x - rad), (int)(sp[i].y - rad),
                (int)(sp[i].x + rad), (int)(sp[i].y + rad));
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(br);

        if ((int)i == g_hover_index) {
            HPEN hp = CreatePen(PS_SOLID, 2, RGB(255, 255, 255));
            HGDIOBJ o = SelectObject(dc, hp);
            HGDIOBJ ob2 = SelectObject(dc, GetStockObject(NULL_BRUSH));
            Ellipse(dc, (int)(sp[i].x - rad - 5), (int)(sp[i].y - rad - 5),
                    (int)(sp[i].x + rad + 5), (int)(sp[i].y + rad + 5));
            SelectObject(dc, o);
            SelectObject(dc, ob2);
            DeleteObject(hp);
        }
    }

    /* Rótulo do hover. */
    SetBkMode(dc, TRANSPARENT);
    if (g_hover_index >= 0 && (size_t)g_hover_index < visible) {
        size_t i = (size_t)g_hover_index;
        char buf[96];
        snprintf(buf, sizeof buf, "n = %llu   passo %zu",
                 (unsigned long long)g_seq[i], i);
        SetTextColor(dc, RGB(242, 247, 255));
        TextOutA(dc, (int)sp[i].x + 10, (int)sp[i].y - 24, buf,
                 (int)strlen(buf));
    }

    /* Rodapé: contador. */
    {
        char info[64];
        snprintf(info, sizeof info, "%zu de %zu nos", visible, g_seq_len);
        SIZE sz;
        GetTextExtentPoint32A(dc, info, (int)strlen(info), &sz);
        SetTextColor(dc, RGB(150, 157, 178));
        TextOutA(dc, area->right - sz.cx - 14, area->bottom - 22, info,
                 (int)strlen(info));
    }

    free(pts);
    free(sp);
    free(order);
    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
}

/* ------------------------------------------------------------------ */
/* Desenho da árvore (grafo de Collatz com fusões)                    */
/* ------------------------------------------------------------------ */

static void tree_screen_win(const RECT *area, double nx, double ny,
                            double zoom, double *sx, double *sy)
{
    double uw = (area->right - area->left) - 2 * 60.0;
    double uh = (area->bottom - area->top) - 2 * 60.0;
    if (uw < 1.0) uw = 1.0;
    if (uh < 1.0) uh = 1.0;
    *sx = area->left + 60.0 + nx * uw * zoom;
    *sy = area->top + 60.0 + ny * uh * zoom;
}

static void draw_tree(HDC dc, RECT *area)
{
    HBRUSH bg = CreateSolidBrush(RGB(14, 16, 26));
    FillRect(dc, area, bg);
    DeleteObject(bg);

    HRGN clip = CreateRectRgn(area->left, area->top, area->right, area->bottom);
    SelectClipRgn(dc, clip);

    SetBkMode(dc, TRANSPARENT);

    if (!g_tree_has_paths || g_tree_graph.node_count == 0) {
        SetTextColor(dc, RGB(140, 148, 168));
        const char *msg =
            "Digite um ou mais numeros e clique em Desenhar arvore.";
        TextOutA(dc, area->left + 30,
                 area->top + (area->bottom - area->top) / 2, msg,
                 (int)strlen(msg));
        SelectClipRgn(dc, NULL);
        DeleteObject(clip);
        return;
    }

    CollatzGraph *g = &g_tree_graph;
    int max_depth = g->max_depth > 0 ? g->max_depth : 1;
    double reveal_speed = max_depth / 3.0;
    if (reveal_speed < 8.0) reveal_speed = 8.0;
    double reveal_depth = g_tree_clock * reveal_speed;
    double zoom = 1.0;

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
            tree_screen_win(area, from->x, from->y, zoom, &ax, &ay);
            tree_screen_win(area, g->nodes[ed->to].x, g->nodes[ed->to].y,
                            zoom, &bx, &by);

            COLORREF col = ed->grew ? RGB(255, 130, 70) : RGB(56, 194, 106);
            int width = ed->highlighted ? 2 : 1;
            HPEN pen = CreatePen(PS_SOLID, width,
                                 ed->highlighted ? col : RGB(
                                    GetRValue(col) / 2 + 30,
                                    GetGValue(col) / 2 + 30,
                                    GetBValue(col) / 2 + 30));
            HGDIOBJ old = SelectObject(dc, pen);
            MoveToEx(dc, (int)ax, (int)ay, NULL);
            LineTo(dc, (int)bx, (int)by);
            SelectObject(dc, old);
            DeleteObject(pen);
        }
    }

    /* Nós. */
    static const COLORREF pal[8] = {
        RGB(255, 140, 64), RGB(112, 219, 140), RGB(140, 168, 255),
        RGB(255, 115, 158), RGB(242, 209, 77), RGB(115, 230, 230),
        RGB(191, 140, 242), RGB(250, 173, 102)
    };
    double pulse = 0.5 + 0.5 * sin(g_tree_clock * 3.0);

    for (size_t i = 0; i < g->node_count; i++) {
        CollatzGraphNode *nd = &g->nodes[i];
        double reveal = (double)(max_depth - nd->depth);
        if (reveal > reveal_depth + 6.0)
            continue;
        if (reveal > reveal_depth && !nd->highlighted)
            continue;

        double x, y;
        tree_screen_win(area, nd->x, nd->y, zoom, &x, &y);
        int one = (nd->value == 1);
        int leaf = nd->is_leaf;
        int trunk = nd->is_trunk;

        double rad = 2.6;
        if (nd->highlighted) rad = 3.4;
        if (trunk) rad = 5.0;
        if (leaf) rad = 6.5;
        if (one) rad = 8.0;
        rad += 1.2 * pulse;

        COLORREF col;
        if (one) col = RGB(120, 240, 180);
        else if (leaf) col = nd->highlighted ? pal[(nd->leaf_color < 0 ? 0
                                                       : nd->leaf_color) % 8]
                                             : RGB(140, 140, 150);
        else if (trunk) col = RGB(56, 194, 106);
        else if (nd->highlighted) col = RGB(143, 196, 255);
        else col = RGB(77, 107, 158);

        HBRUSH br = CreateSolidBrush(col);
        HGDIOBJ ob = SelectObject(dc, br);
        HGDIOBJ op = SelectObject(dc, GetStockObject(NULL_PEN));
        Ellipse(dc, (int)(x - rad), (int)(y - rad),
                (int)(x + rad), (int)(y + rad));
        SelectObject(dc, ob);
        SelectObject(dc, op);
        DeleteObject(br);

        if (leaf || trunk || one) {
            char lab[24];
            snprintf(lab, sizeof lab, "%llu",
                     (unsigned long long)nd->value);
            SetTextColor(dc, RGB(245, 248, 255));
            TextOutA(dc, (int)(x + rad + 4), (int)(y - 8), lab,
                     (int)strlen(lab));
        }
    }

    /* Legenda. */
    SetTextColor(dc, RGB(204, 212, 230));
    HBRUSH lc = CreateSolidBrush(RGB(255, 130, 70));
    RECT r1 = {area->left + 14, area->bottom - 26, area->left + 24,
               area->bottom - 16};
    FillRect(dc, &r1, lc);
    DeleteObject(lc);
    TextOutA(dc, area->left + 30, area->bottom - 26, "3n+1 (cresce)", 13);
    HBRUSH lc2 = CreateSolidBrush(RGB(56, 194, 106));
    RECT r2 = {area->left + 160, area->bottom - 26, area->left + 170,
               area->bottom - 16};
    FillRect(dc, &r2, lc2);
    DeleteObject(lc2);
    TextOutA(dc, area->left + 176, area->bottom - 26, "n/2 (cai)", 9);

    size_t trunks = 0;
    for (size_t i = 0; i < g->node_count; i++)
        if (g->nodes[i].is_trunk) trunks++;
    char info[96];
    snprintf(info, sizeof info, "%zu nos - %zu fusoes - %d niveis",
             g->node_count, trunks, g->max_depth);
    SIZE sz;
    GetTextExtentPoint32A(dc, info, (int)strlen(info), &sz);
    SetTextColor(dc, RGB(160, 168, 190));
    TextOutA(dc, area->right - sz.cx - 14, area->bottom - 26, info,
             (int)strlen(info));

    SelectClipRgn(dc, NULL);
    DeleteObject(clip);
}

/* Constrói o grafo da árvore a partir do campo de texto. */
static void tree_build_from_field(void)
{
    char text[1024];
    GetWindowTextA(g_entry_trees, text, sizeof text);

    uint64_t sel[1024];
    size_t n = 0;
    const char *p = text;
    while (*p != '\0' && n < 1024) {
        while (*p == ' ' || *p == ',' || *p == ';' || *p == '\t')
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
        SetWindowTextA(g_lbl_stats, "Digite um ou mais numeros validos.");
        return;
    }

    if (g_tree_has_paths) {
        collatz_graph_free(&g_tree_graph);
        g_tree_has_paths = 0;
    }
    if (collatz_graph_build(sel, n, &g_tree_graph) == 0) {
        g_tree_has_paths = 1;
        g_tree_clock = 0.0;
        size_t trunks = 0;
        for (size_t i = 0; i < g_tree_graph.node_count; i++)
            if (g_tree_graph.nodes[i].is_trunk) trunks++;
        char stats[160];
        snprintf(stats, sizeof stats,
                 "%zu numero(s) - %zu nos - %zu fusoes",
                 n, g_tree_graph.node_count, trunks);
        SetWindowTextA(g_lbl_stats, stats);
    } else {
        SetWindowTextA(g_lbl_stats, "Nao foi possivel construir a arvore.");
    }
    InvalidateRect(g_hwnd, NULL, FALSE);
}

/* ------------------------------------------------------------------ */
/* Geração da sequência                                               */
/* ------------------------------------------------------------------ */

static void generate_for(HWND hwnd, uint64_t start)
{
    size_t cap = 4096;
    uint64_t *buf = (uint64_t *)malloc(cap * sizeof(uint64_t));
    if (buf == NULL) return;

    size_t len = 0;
    int ovf = 0;
    while (collatz_generate(start, buf, cap, &len, &ovf) != 0) {
        cap *= 2;
        uint64_t *nb = (uint64_t *)realloc(buf, cap * sizeof(uint64_t));
        if (nb == NULL) { free(buf); return; }
        buf = nb;
    }

    free(g_seq);
    g_seq = buf;
    g_seq_len = len;
    g_seq_overflow = ovf;
    g_seq_start = start;
    g_hover_index = -1;
    g_clock = 0.0;
    g_rot_yaw = -0.35;
    g_rot_pitch = 0.12;

    uint64_t max_v = g_seq[0];
    for (size_t i = 1; i < len; i++)
        if (g_seq[i] > max_v) max_v = g_seq[i];

    char stats[256];
    snprintf(stats, sizeof stats,
             "Numero: %llu     Passos ate 1: %zu     Maior valor: %llu",
             (unsigned long long)start, len - 1, (unsigned long long)max_v);
    SetWindowTextA(g_lbl_stats, stats);

    if (ovf)
        SetWindowTextA(g_lbl_record,
                       "Aviso: a sequencia ultrapassou 64 bits; truncada.");
    else
        SetWindowTextA(g_lbl_record,
                       "Sequencia gerada. Arraste no painel para girar.");

    InvalidateRect(hwnd, NULL, FALSE);
}

static void on_generate_clicked(void)
{
    char text[64];
    GetWindowTextA(g_entry_start, text, sizeof text);
    uint64_t start;
    if (!parse_u64(text, &start) || start == 0) {
        SetWindowTextA(g_lbl_record,
                       "Digite um numero inteiro maior ou igual a 1.");
        return;
    }
    generate_for(g_hwnd, start);
}

/* ------------------------------------------------------------------ */
/* Thread da busca por recordes                                       */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t start, end;
    unsigned threads;
    uint64_t max_steps;
} SearchArgs;

static void progress_trampoline(double frac, void *ud)
{
    (void)ud;
    g_progress_fraction = frac;
}

static void update_progress_ui(void)
{
    int pct = (int)(g_progress_fraction * 100.0);
    SendMessage(g_progress, PBM_SETPOS, (WPARAM)pct, 0);
}

static DWORD WINAPI search_thread(LPVOID param)
{
    SearchArgs *a = (SearchArgs *)param;
    CollatzRecord rec = {0, 0};
    int st = collatz_search(a->start, a->end, a->threads, a->max_steps, 0,
                            &rec, progress_trampoline, NULL,
                            (volatile int *)&g_search_cancel);
    g_pending_record = rec;
    g_search_status = st;
    free(a);
    InterlockedExchange(&g_search_done, 1);
    return 0;
}

static void on_search_clicked(void)
{
    if (InterlockedCompareExchange(&g_search_running, 1, 0) != 0)
        return;

    char t1[64], t2[64];
    GetWindowTextA(g_entry_rstart, t1, sizeof t1);
    GetWindowTextA(g_entry_rend, t2, sizeof t2);
    uint64_t a, b;
    if (!parse_u64(t1, &a) || !parse_u64(t2, &b) || a == 0 || b < a) {
        InterlockedExchange(&g_search_running, 0);
        SetWindowTextA(g_lbl_record,
                       "Faixa invalida: inicio >= 1 e fim >= inicio.");
        return;
    }

    SearchArgs *args = (SearchArgs *)calloc(1, sizeof(SearchArgs));
    args->start = a;
    args->end = b;
    args->threads = collatz_cpu_count();
    args->max_steps = 100000;

    InterlockedExchange(&g_search_cancel, 0);
    InterlockedExchange(&g_search_done, 0);
    g_progress_fraction = 0.0;
    SendMessage(g_progress, PBM_SETPOS, 0, 0);
    SetWindowTextA(g_lbl_record, "Calculando...");

    HANDLE h = CreateThread(NULL, 0, search_thread, args, 0, NULL);
    if (h == NULL) {
        free(args);
        InterlockedExchange(&g_search_running, 0);
        SetWindowTextA(g_lbl_record, "Nao foi possivel iniciar a busca.");
        return;
    }
    CloseHandle(h);
}

static void on_search_poll(void)
{
    update_progress_ui();

    if (!g_search_running)
        return;
    if (InterlockedCompareExchange(&g_search_done, 0, 0) == 0)
        return;

    InterlockedExchange(&g_search_running, 0);
    SendMessage(g_progress, PBM_SETPOS, 100, 0);

    char msg[256];
    if (g_search_status == 0 && g_pending_record.n != 0) {
        snprintf(msg, sizeof msg, "Recorde na faixa: n = %llu com %llu passos",
                 (unsigned long long)g_pending_record.n,
                 (unsigned long long)g_pending_record.steps);
    } else if (g_search_status == 1) {
        snprintf(msg, sizeof msg,
                 "Busca cancelada/limitada. Melhor: n = %llu (%llu passos)",
                 (unsigned long long)g_pending_record.n,
                 (unsigned long long)g_pending_record.steps);
    } else {
        snprintf(msg, sizeof msg, "Nenhum resultado.");
    }
    SetWindowTextA(g_lbl_record, msg);
}

/* ------------------------------------------------------------------ */
/* Hover                                                              */
/* ------------------------------------------------------------------ */

static void update_hover(int mx, int my)
{
    if (g_seq_len == 0 || g_hwnd == NULL) return;

    RECT rc;
    GetClientRect(g_hwnd, &rc);
    /* A área do painel fica abaixo do grupo de recordes; usamos a mesma
     * região de desenho guardada em g_viz_area. */
    int use3d = (SendMessage(g_chk_3d, BM_GETCHECK, 0, 0) == BST_CHECKED);
    size_t visible = g_seq_len > MAX_NODES ? MAX_NODES : g_seq_len;

    double max_v = 1.0;
    for (size_t i = 0; i < visible; i++)
        if ((double)g_seq[i] > max_v) max_v = (double)g_seq[i];
    double log_max = log(max_v) > 0 ? log(max_v) : 1.0;

    int width = rc.right - rc.left;
    int height = rc.bottom - rc.top;
    double scale = (width < 700) ? width / 700.0 : 0.85;
    double cx = width * 0.5;
    double cy = height * 0.5 + 20.0;
    double yaw = use3d ? g_rot_yaw : 0.0;
    double pitch = use3d ? g_rot_pitch : 0.0;

    int hover = -1;
    double best = 22.0 * 22.0;
    for (size_t i = 0; i < visible; i++) {
        double t = (visible > 1) ? (double)i / (double)(visible - 1) : 0.0;
        double v = (double)g_seq[i];
        double ly = log(v) > 0 ? log(v) : 0.0;
        V3 p;
        p.x = (t - 0.5) * 900.0;
        p.y = -(ly / log_max) * 380.0 + 190.0;
        p.z = use3d ? sin(t * 16.0) * 130.0 : 0.0;
        V3 s = project_point(&p, yaw, pitch, cx, cy, scale);
        double dx = s.x - mx, dy = s.y - my;
        double d2 = dx * dx + dy * dy;
        if (d2 < best) { best = d2; hover = (int)i; }
    }
    if (hover != g_hover_index) {
        g_hover_index = hover;
        InvalidateRect(g_hwnd, NULL, FALSE);
    }
}

/* ------------------------------------------------------------------ */
/* Janela                                                             */
/* ------------------------------------------------------------------ */

static HWND mk_button(HWND parent, const char *txt, int x, int y, int w, int h,
                      int id)
{
    return CreateWindowA("BUTTON", txt, WS_CHILD | WS_VISIBLE | BS_PUSHBUTTON,
                         x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
}

static HWND mk_check(HWND parent, const char *txt, int x, int y, int w, int h,
                     int id, int checked)
{
    HWND hw = CreateWindowA("BUTTON", txt,
                            WS_CHILD | WS_VISIBLE | BS_AUTOCHECKBOX,
                            x, y, w, h, parent, (HMENU)(INT_PTR)id, NULL, NULL);
    SendMessage(hw, BM_SETCHECK, checked ? BST_CHECKED : BST_UNCHECKED, 0);
    return hw;
}

static void build_controls(HWND hwnd)
{
    HFONT font = (HFONT)GetStockObject(DEFAULT_GUI_FONT);

    /* Linha 1: modo de visualizacao. */
    g_radio_seq = CreateWindowA("BUTTON", "Sequencia",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON | WS_GROUP,
                    12, 12, 90, 22, hwnd, (HMENU)ID_RADIO_SEQ, NULL, NULL);
    SendMessage(g_radio_seq, BM_SETCHECK, BST_CHECKED, 0);
    g_radio_tree = CreateWindowA("BUTTON", "Arvore",
                    WS_CHILD | WS_VISIBLE | BS_AUTORADIOBUTTON,
                    104, 12, 70, 22, hwnd, (HMENU)ID_RADIO_TREE, NULL, NULL);

    /* Linha 2: controles da sequencia. */
    CreateWindowA("STATIC", "Numero de partida:", WS_CHILD | WS_VISIBLE,
                  12, 42, 140, 20, hwnd, NULL, NULL, NULL);
    g_entry_start = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "27",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    158, 40, 180, 24, hwnd, (HMENU)ID_ENTRY_START, NULL, NULL);
    g_btn_generate = mk_button(hwnd, "Gerar sequencia", 348, 40, 130, 26,
                               ID_BTN_GENERATE);
    g_chk_3d = mk_check(hwnd, "Modo 3D", 500, 42, 90, 22, ID_CHK_3D, 1);
    g_chk_rotate = mk_check(hwnd, "Girar automaticamente", 596, 42, 160, 22,
                            ID_CHK_ROTATE, 1);

    /* Linha 2 (modo arvore): numeros + botao. */
    g_lbl_treeNums = CreateWindowA("STATIC", "Numeros:", WS_CHILD,
                    12, 42, 66, 20, hwnd, NULL, NULL, NULL);
    g_entry_trees = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT",
                    "27, 97, 871, 41",
                    WS_CHILD | ES_AUTOHSCROLL,
                    82, 40, 300, 24, hwnd, (HMENU)ID_ENTRY_TREES, NULL, NULL);
    g_btn_treedraw = mk_button(hwnd, "Desenhar arvore", 392, 39, 140, 26,
                               ID_BTN_TREEDRAW);
    ShowWindow(g_btn_treedraw, SW_HIDE);
    ShowWindow(g_lbl_treeNums, SW_HIDE);
    ShowWindow(g_entry_trees, SW_HIDE);

    /* Linha 3: estatisticas. */
    g_lbl_stats = CreateWindowA("STATIC",
                    "Informe um numero e clique em Gerar.",
                    WS_CHILD | WS_VISIBLE, 12, 72, 738, 20, hwnd, NULL, NULL,
                    NULL);

    /* Linha 4: grupo de recordes. */
    CreateWindowA("STATIC", "Inicio:", WS_CHILD | WS_VISIBLE,
                  12, 102, 44, 20, hwnd, NULL, NULL, NULL);
    g_entry_rstart = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "1",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    56, 100, 70, 24, hwnd, (HMENU)ID_ENTRY_RSTART, NULL, NULL);
    CreateWindowA("STATIC", "Fim:", WS_CHILD | WS_VISIBLE,
                  132, 102, 32, 20, hwnd, NULL, NULL, NULL);
    g_entry_rend = CreateWindowExA(WS_EX_CLIENTEDGE, "EDIT", "1000000",
                    WS_CHILD | WS_VISIBLE | ES_AUTOHSCROLL,
                    168, 100, 90, 24, hwnd, (HMENU)ID_ENTRY_REND, NULL, NULL);
    g_btn_search = mk_button(hwnd, "Buscar recorde", 266, 99, 110, 26,
                             ID_BTN_SEARCH);
    g_btn_cancel = mk_button(hwnd, "Cancelar", 382, 99, 80, 26, ID_BTN_CANCEL);

    g_progress = CreateWindowExA(0, PROGRESS_CLASSA, NULL,
                    WS_CHILD | WS_VISIBLE, 470, 100, 280, 20, hwnd,
                    (HMENU)ID_PROGRESS, NULL, NULL);

    g_lbl_record = CreateWindowA("STATIC", "", WS_CHILD | WS_VISIBLE,
                    12, 130, 738, 20, hwnd, (HMENU)ID_LBL_RECORD, NULL, NULL);

    /* Aplica a fonte padrão aos controles. */
    HWND all[] = {g_entry_start, g_btn_generate, g_lbl_stats, g_chk_3d,
                  g_chk_rotate, g_entry_rstart, g_entry_rend, g_btn_search,
                  g_btn_cancel, g_progress, g_lbl_record, g_radio_seq,
                  g_radio_tree, g_entry_trees, g_btn_treedraw, g_lbl_treeNums};
    for (int i = 0; i < (int)(sizeof(all) / sizeof(all[0])); i++)
        SendMessage(all[i], WM_SETFONT, (WPARAM)font, TRUE);
}

static void apply_mode(HWND hwnd, int tree)
{
    (void)hwnd;
    g_show_tree = tree;

    int seq_show = tree ? SW_HIDE : SW_SHOW;
    int tree_show = tree ? SW_SHOW : SW_HIDE;

    /* Controles exclusivos da sequencia. */
    ShowWindow(g_entry_start, seq_show);
    ShowWindow(g_btn_generate, seq_show);
    ShowWindow(g_chk_3d, seq_show);
    ShowWindow(g_chk_rotate, seq_show);

    /* Controles exclusivos da arvore. */
    ShowWindow(g_lbl_treeNums, tree_show);
    ShowWindow(g_entry_trees, tree_show);
    ShowWindow(g_btn_treedraw, tree_show);

    if (tree)
        SetWindowTextA(g_lbl_stats,
                       "Digite os numeros e clique em Desenhar arvore.");
    else
        SetWindowTextA(g_lbl_stats,
                       "Informe um numero e clique em Gerar.");

    InvalidateRect(g_hwnd, NULL, FALSE);
}

static LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    switch (msg) {    case WM_CREATE:
        build_controls(hwnd);
        SetTimer(hwnd, ID_TIMER_ANIM, 33, NULL);
        return 0;

    case WM_COMMAND:
        switch (LOWORD(wp)) {
        case ID_BTN_GENERATE:
            on_generate_clicked();
            return 0;
        case ID_BTN_SEARCH:
            on_search_clicked();
            return 0;
        case ID_BTN_CANCEL:
            InterlockedExchange(&g_search_cancel, 1);
            SetWindowTextA(g_lbl_record, "Cancelando...");
            return 0;
        case ID_CHK_3D:
            InvalidateRect(hwnd, NULL, FALSE);
            return 0;
        case ID_RADIO_SEQ:
            apply_mode(hwnd, 0);
            return 0;
        case ID_RADIO_TREE:
            apply_mode(hwnd, 1);
            tree_build_from_field();
            return 0;
        case ID_BTN_TREEDRAW:
            tree_build_from_field();
            return 0;
        }
        return 0;

    case WM_TIMER:
        if (wp == ID_TIMER_ANIM) {
            g_clock += 0.033;
            g_tree_clock += 0.033;
            if (!g_show_tree &&
                SendMessage(g_chk_rotate, BM_GETCHECK, 0, 0) == BST_CHECKED &&
                SendMessage(g_chk_3d, BM_GETCHECK, 0, 0) == BST_CHECKED) {
                g_rot_yaw += 0.006;
                if (g_rot_yaw > 6.28318530718)
                    g_rot_yaw -= 6.28318530718;
                g_rot_pitch = sin(g_clock * 0.4) * 0.18;
            }
            on_search_poll();
            InvalidateRect(hwnd, NULL, FALSE);
        }
        return 0;

    case WM_LBUTTONDOWN:
        SendMessage(g_chk_rotate, BM_SETCHECK, BST_UNCHECKED, 0);
        g_dragging = 1;
        g_drag_x = GET_X_LPARAM(lp);
        g_drag_y = GET_Y_LPARAM(lp);
        SetCapture(hwnd);
        return 0;

    case WM_MOUSEMOVE:
        {
            int mx = GET_X_LPARAM(lp), my = GET_Y_LPARAM(lp);
            if (g_dragging) {
                g_rot_yaw += (mx - g_drag_x) * 0.01;
                g_rot_pitch += (my - g_drag_y) * 0.006;
                if (g_rot_pitch > 0.9) g_rot_pitch = 0.9;
                if (g_rot_pitch < -0.9) g_rot_pitch = -0.9;
                g_drag_x = mx; g_drag_y = my;
                InvalidateRect(hwnd, NULL, FALSE);
            }
            update_hover(mx, my);
        }
        return 0;

    case WM_LBUTTONUP:
        g_dragging = 0;
        ReleaseCapture();
        return 0;

    case WM_ERASEBKGND:
        return 1; /* evita piscar; pintamos tudo no WM_PAINT */

    case WM_PAINT:
        {
            PAINTSTRUCT ps;
            HDC dc = BeginPaint(hwnd, &ps);
            RECT rc;
            GetClientRect(hwnd, &rc);

            /* Fundo geral claro. */
            HBRUSH bg = CreateSolidBrush(GetSysColor(COLOR_BTNFACE));
            FillRect(dc, &rc, bg);
            DeleteObject(bg);

            /* Área do painel animado. */
            RECT area;
            area.left = 12;
            area.top = 150;
            area.right = rc.right - 12;
            area.bottom = rc.bottom - 12;
            if (area.bottom < area.top + 50)
                area.bottom = area.top + 50;
            if (g_show_tree)
                draw_tree(dc, &area);
            else
                draw_sequence(dc, &area);

            EndPaint(hwnd, &ps);
        }
        return 0;

    case WM_DESTROY:
        KillTimer(hwnd, ID_TIMER_ANIM);
        if (g_tree_has_paths)
            collatz_graph_free(&g_tree_graph);
        PostQuitMessage(0);
        return 0;
    }
    return DefWindowProcA(hwnd, msg, wp, lp);
}

int WINAPI WinMain(HINSTANCE hi, HINSTANCE hp, LPSTR cmdline, int show)
{
    (void)hp;

    WNDCLASSA wc = {0};
    wc.lpfnWndProc = WndProc;
    wc.hInstance = hi;
    wc.hCursor = LoadCursor(NULL, IDC_ARROW);
    wc.hbrBackground = (HBRUSH)(COLOR_BTNFACE + 1);
    wc.lpszClassName = "CollatzWin32";
    RegisterClassA(&wc);

    g_hwnd = CreateWindowExA(0, "CollatzWin32",
                             "Explorador de Collatz (Win32)",
                             WS_OVERLAPPEDWINDOW,
                             CW_USEDEFAULT, CW_USEDEFAULT, 800, 620,
                             NULL, NULL, hi, NULL);
    if (g_hwnd == NULL)
        return 1;

    ShowWindow(g_hwnd, show);
    UpdateWindow(g_hwnd);

    /* Linha de comando: "27" gera a sequencia; "tree 27,97" abre a arvore. */
    if (cmdline != NULL && cmdline[0] != '\0') {
        const char *p = cmdline;
        while (*p == ' ' || *p == '"') p++;
        if ((p[0] == 't' || p[0] == 'T') &&
            (p[1] == 'r' || p[1] == 'R') &&
            (p[2] == 'e' || p[2] == 'E') &&
            (p[3] == 'e' || p[3] == 'E')) {
            p += 4;
            while (*p == ' ' || *p == '"') p++;
            if (*p != '\0') {
                SetWindowTextA(g_entry_trees, p);
                SendMessage(g_hwnd, WM_COMMAND,
                            MAKEWPARAM(ID_RADIO_TREE, BN_CLICKED), 0);
                SetWindowTextA(g_lbl_stats, "Arvore desenhada.");
            }
        } else {
            uint64_t v;
            if (parse_u64(p, &v) && v != 0) {
                SetWindowTextA(g_entry_start, p);
                generate_for(g_hwnd, v);
            }
        }
    }

    MSG m;
    while (GetMessageA(&m, NULL, 0, 0) > 0) {
        TranslateMessage(&m);
        DispatchMessageA(&m);
    }
    free(g_seq);
    return 0;
}
