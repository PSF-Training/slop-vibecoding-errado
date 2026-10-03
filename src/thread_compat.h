/*
 * thread_compat.h - Camada fina de portabilidade de threads.
 *
 * No Linux usa pthreads; no Windows (compilado pelo winegcc) usa as
 * APIs nativas do Windows. O motor de Collatz só usa mutex, criação,
 * junção de threads e contagem de CPUs, então essa abstração cobre tudo.
 */
#ifndef THREAD_COMPAT_H
#define THREAD_COMPAT_H

#ifdef _WIN32

#include <stdlib.h>
#include <string.h>
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>

typedef HANDLE thread_t;
typedef CRITICAL_SECTION thread_mutex_t;

/* Função de thread no estilo pthread. */
typedef void *(*thread_fn_t)(void *);

/* Encapsula a chamada para casar com a assinatura DWORD WINAPI. */
typedef struct {
    thread_fn_t fn;
    void *arg;
} thread_start_t;

static DWORD WINAPI thread_trampoline(LPVOID p)
{
    thread_start_t *s = (thread_start_t *)p;
    thread_fn_t fn = s->fn;
    void *arg = s->arg;
    free(s);
    fn(arg);
    return 0;
}

static int thread_create(thread_t *t, thread_fn_t fn, void *arg)
{
    thread_start_t *s = (thread_start_t *)malloc(sizeof(thread_start_t));
    if (s == NULL)
        return -1;
    s->fn = fn;
    s->arg = arg;
    *t = CreateThread(NULL, 0, thread_trampoline, s, 0, NULL);
    if (*t == NULL) {
        free(s);
        return -1;
    }
    return 0;
}

static void thread_join(thread_t t)
{
    if (t != NULL) {
        WaitForSingleObject(t, INFINITE);
        CloseHandle(t);
    }
}

static void mutex_init(thread_mutex_t *m) { InitializeCriticalSection(m); }
static void mutex_lock(thread_mutex_t *m) { EnterCriticalSection(m); }
static void mutex_unlock(thread_mutex_t *m) { LeaveCriticalSection(m); }

static void sleep_ms(unsigned ms) { Sleep(ms); }

static unsigned cpu_count(void)
{
    SYSTEM_INFO si;
    GetSystemInfo(&si);
    return (unsigned)si.dwNumberOfProcessors;
}

/* Relógio monotônico em milissegundos. */
static unsigned long long monotonic_ms(void)
{
    /* GetTickCount64 é estável e não sofre ajustes de relógio. */
    return (unsigned long long)GetTickCount64();
}

#else /* POSIX */

#include <pthread.h>
#include <stdlib.h>
#include <unistd.h>
#include <time.h>

typedef pthread_t thread_t;
typedef pthread_mutex_t thread_mutex_t;
typedef void *(*thread_fn_t)(void *);

static int thread_create(thread_t *t, thread_fn_t fn, void *arg)
{
    return pthread_create(t, NULL, fn, arg);
}

static void thread_join(thread_t t) { pthread_join(t, NULL); }

static void mutex_init(thread_mutex_t *m) { pthread_mutex_init(m, NULL); }
static void mutex_lock(thread_mutex_t *m) { pthread_mutex_lock(m); }
static void mutex_unlock(thread_mutex_t *m) { pthread_mutex_unlock(m); }

static void sleep_ms(unsigned ms)
{
    struct timespec ts;
    ts.tv_sec = ms / 1000;
    ts.tv_nsec = (long)(ms % 1000) * 1000000L;
    nanosleep(&ts, NULL);
}

static unsigned cpu_count(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return (n > 0) ? (unsigned)n : 1u;
}

/* Relógio monotônico em milissegundos. */
static unsigned long long monotonic_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (unsigned long long)ts.tv_sec * 1000ULL +
           (unsigned long long)ts.tv_nsec / 1000000ULL;
}

#endif /* _WIN32 */

#endif /* THREAD_COMPAT_H */
