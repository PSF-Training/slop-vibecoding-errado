/*
 * collatz.c - Implementação do motor de cálculo da sequência de Collatz.
 *
 * Estratégia de desempenho:
 *   - C com aritmética de 128 bits para detectar overflow sem travar.
 *   - Memoização (cache) guardando a parada conhecida e os passos de
 *     cada valor já visitado, evitando recomputar caudas inteiras.
 *   - Paralelismo com pthreads: a faixa é dividida em pedaços contíguos.
 *   - Limites de passos e de tempo para nunca travar a interface.
 */
#define _POSIX_C_SOURCE 200809L
#include "collatz.h"
#include "thread_compat.h"

#include <stdlib.h>
#include <string.h>
#include <time.h>

/* ------------------------------------------------------------------ */
/* Tempo                                                              */
/* ------------------------------------------------------------------ */

static uint64_t now_ms(void)
{
    return (uint64_t)monotonic_ms();
}

/* ------------------------------------------------------------------ */
/* Geração de uma sequência arbitrária                                */
/* ------------------------------------------------------------------ */

int collatz_generate(uint64_t start, uint64_t *out, size_t capacity,
                     size_t *out_len, int *overflow)
{
    if (start == 0 || out_len == NULL || overflow == NULL)
        return -1;

    size_t len = 0;
    int ovf = 0;
    __uint128_t x = start;

    for (;;) {
        if (out != NULL) {
            if (len >= capacity)
                return -1;
            out[len] = (uint64_t)x;
        }
        len++;

        if (x == 1)
            break;

        if ((x & 1u) == 0u) {
            x >>= 1;
        } else {
            __uint128_t next = 3u * x + 1u;
            /* Se ultrapassaria 64 bits, registramos o overflow e paramos. */
            if (next > (__uint128_t)UINT64_MAX) {
                ovf = 1;
                break;
            }
            x = next;
        }
    }

    *out_len = len;
    *overflow = ovf;
    return 0;
}

/* ------------------------------------------------------------------ */
/* Contagem de passos (com proteção contra overflow)                  */
/* ------------------------------------------------------------------ */

uint64_t collatz_steps(uint64_t start)
{
    if (start == 0)
        return UINT64_MAX;

    uint64_t steps = 0;
    __uint128_t x = start;

    while (x != 1) {
        if ((x & 1u) == 0u) {
            x >>= 1;
        } else {
            __uint128_t next = 3u * x + 1u;
            if (next > (__uint128_t)UINT64_MAX)
                return UINT64_MAX; /* não computável com segurança */
            x = next;
        }
        steps++;

        /* Trava de segurança adicional: nenhuma sequência de Collatz
         * conhecida passa disso, mas evitamos loop infinito teórico. */
        if (steps > 100000000ULL)
            return UINT64_MAX;
    }

    return steps;
}

/* ------------------------------------------------------------------ */
/* Cache de memoização compartilhado                                  */
/* ------------------------------------------------------------------ */

#define CACHE_BITS 24
#define CACHE_SIZE (1u << CACHE_BITS)
#define CACHE_MASK (CACHE_SIZE - 1u)

typedef struct {
    uint64_t key;   /* chave (valor) consultada; 0 = vazio   */
    uint64_t steps; /* passos até 1 a partir dessa chave     */
} CacheEntry;

static CacheEntry *g_cache;
static thread_mutex_t g_cache_mtx;
static int g_cache_mtx_ready = 0;

/* Garante o mutex inicializado (chamado uma vez, da thread principal). */
static void cache_mutex_ensure(void)
{
    if (!g_cache_mtx_ready) {
        mutex_init(&g_cache_mtx);
        g_cache_mtx_ready = 1;
    }
}

/* Consulta o cache sem bloqueio. Retorna 1 se encontrou. */
static int cache_lookup(uint64_t key, uint64_t *steps)
{
    if (g_cache == NULL || key == 0)
        return 0;
    const CacheEntry *e = &g_cache[key & CACHE_MASK];
    if (e->key == key) {
        *steps = e->steps;
        return 1;
    }
    return 0;
}

/* Insere no cache de forma thread-safe (best effort). */
static void cache_store(uint64_t key, uint64_t steps)
{
    if (g_cache == NULL || key == 0 || key > (uint64_t)(CACHE_SIZE - 1))
        return;
    mutex_lock(&g_cache_mtx);
    /* Recheque dentro do lock para não sobrepor uma entrada boa. */
    CacheEntry *e = &g_cache[key & CACHE_MASK];
    if (e->key != key) {
        e->key = key;
        e->steps = steps;
    }
    mutex_unlock(&g_cache_mtx);
}

/* Conta passos usando o cache, registrando o caminho percorrido para
 * preencher o cache de volta. */
static uint64_t steps_cached(uint64_t start)
{
    uint64_t local[512];
    size_t local_n = 0;
    uint64_t x = start;
    uint64_t result;

    for (;;) {
        uint64_t cached;
        if (cache_lookup(x, &cached)) {
            result = cached;
            break;
        }

        if (x == 1) {
            result = 0;
            break;
        }

        if (local_n < sizeof(local) / sizeof(local[0]))
            local[local_n++] = x;

        if ((x & 1u) == 0u) {
            x >>= 1;
        } else {
            __uint128_t next = 3u * x + 1u;
            if (next > (__uint128_t)UINT64_MAX) {
                result = UINT64_MAX; /* sinaliza "desconhecido" */
                break;
            }
            x = (uint64_t)next;
        }
    }

    if (result != UINT64_MAX) {
        /* Preenche o cache de trás para frente: cada valor do caminho
         * local está a um passo extra do seguinte. */
        uint64_t steps = result;
        for (size_t i = local_n; i > 0; i--) {
            steps += 1;
            cache_store(local[i - 1], steps);
        }
        result = steps;
    }

    return result;
}

/* ------------------------------------------------------------------ */
/* Busca paralela por recordes                                        */
/* ------------------------------------------------------------------ */

typedef struct {
    uint64_t lo;   /* início do pedaço (inclusivo) */
    uint64_t hi;   /* fim do pedaço (exclusivo)    */
    uint64_t max_steps;
    uint64_t time_budget_ms;
    volatile int *canceled;
    volatile uint64_t *progress_counter; /* valores processados       */
    volatile int *stop_flag;             /* encerrar todas as threads */

    /* Saída parcial da thread. */
    uint64_t best_n;
    uint64_t best_steps;
    int canceled_out;
} Worker;

static void *worker_main(void *arg)
{
    Worker *w = (Worker *)arg;
    uint64_t best_n = 0;
    uint64_t best_steps = 0;
    const uint64_t deadline = (w->time_budget_ms > 0)
                                  ? now_ms() + w->time_budget_ms
                                  : 0;

    for (uint64_t n = w->lo; n < w->hi; n++) {
        if (*w->stop_flag || (w->canceled != NULL && *w->canceled)) {
            w->canceled_out = 1;
            break;
        }
        if (deadline != 0 && (n & 0x3FFu) == 0u && now_ms() > deadline) {
            *w->stop_flag = 1;
            w->canceled_out = 1;
            break;
        }

        uint64_t steps = steps_cached(n);
        *w->progress_counter += 1;

        if (steps == UINT64_MAX)
            continue; /* overflow: pulamos com segurança */

        if (steps > best_steps) {
            best_steps = steps;
            best_n = n;
        }
        if (w->max_steps > 0 && steps > w->max_steps) {
            /* Número muito custoso: encerra a busca como proteção. */
            *w->stop_flag = 1;
            w->canceled_out = 1;
            break;
        }
    }

    w->best_n = best_n;
    w->best_steps = best_steps;
    return NULL;
}

int collatz_search(uint64_t start, uint64_t end, unsigned n_threads,
                   uint64_t max_steps, uint64_t time_budget_ms,
                   CollatzRecord *record,
                   void (*progress)(double fraction, void *ud), void *progress_ud,
                   volatile int *canceled)
{
    if (record == NULL || start == 0 || end < start)
        return -1;

    if (n_threads == 0)
        n_threads = collatz_cpu_count();
    if (n_threads > 256)
        n_threads = 256;

    /* Aloca/limpa o cache. */
    cache_mutex_ensure();
    if (g_cache == NULL) {
        g_cache = calloc(CACHE_SIZE, sizeof(CacheEntry));
        if (g_cache == NULL)
            return -1;
    } else {
        mutex_lock(&g_cache_mtx);
        memset(g_cache, 0, CACHE_SIZE * sizeof(CacheEntry));
        mutex_unlock(&g_cache_mtx);
    }

    thread_t *tids = calloc(n_threads, sizeof(thread_t));
    Worker *workers = calloc(n_threads, sizeof(Worker));
    if (tids == NULL || workers == NULL) {
        free(tids);
        free(workers);
        return -1;
    }

    volatile uint64_t progress_counter = 0;
    volatile int stop_flag = 0;
    const uint64_t span = end - start + 1;
    const uint64_t chunk = (span + n_threads - 1) / n_threads;

    for (unsigned i = 0; i < n_threads; i++) {
        uint64_t lo = start + (uint64_t)i * chunk;
        uint64_t hi = lo + chunk;
        if (hi > end + 1)
            hi = end + 1;
        workers[i].lo = lo;
        workers[i].hi = hi;
        workers[i].max_steps = max_steps;
        workers[i].time_budget_ms = time_budget_ms;
        workers[i].canceled = canceled;
        workers[i].progress_counter = &progress_counter;
        workers[i].stop_flag = &stop_flag;
        workers[i].best_n = 0;
        workers[i].best_steps = 0;
        workers[i].canceled_out = 0;
        if (lo <= end)
            thread_create(&tids[i], worker_main, &workers[i]);
        else
            tids[i] = 0; /* pedaço vazio */
    }

    /* Monitora o progresso enquanto as threads trabalham. */
    if (progress != NULL) {
        int running = 1;
        while (running) {
            uint64_t done = progress_counter;
            double frac = span ? (double)done / (double)span : 1.0;
            if (frac > 1.0)
                frac = 1.0;
            progress(frac, progress_ud);

            /* Detecta término: todas as threads já sinalizaram parada ou
             * o contador alcançou o total. */
            running = 0;
            for (unsigned i = 0; i < n_threads; i++)
                if (workers[i].hi > workers[i].lo && workers[i].best_n == 0 &&
                    !stop_flag)
                    running = 1;
            if (done >= span)
                running = 0;
            if (stop_flag)
                running = 0;

            if (running)
                sleep_ms(50);
        }
        progress(1.0, progress_ud);
    }

    int canceled_any = 0;
    CollatzRecord best = {0, 0};
    for (unsigned i = 0; i < n_threads; i++) {
        if (tids[i] == 0)
            continue;
        thread_join(tids[i]);
        if (workers[i].canceled_out)
            canceled_any = 1;
        if (workers[i].best_n != 0 && workers[i].best_steps > best.steps) {
            best.steps = workers[i].best_steps;
            best.n = workers[i].best_n;
        }
    }

    free(tids);
    free(workers);

    *record = best;
    if (canceled_any || (canceled != NULL && *canceled))
        return 1;
    return 0;
}

unsigned collatz_cpu_count(void)
{
    return cpu_count();
}
