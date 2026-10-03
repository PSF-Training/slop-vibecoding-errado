/*
 * test_win32.c - Testa o motor de Collatz compilado para Windows (Wine).
 *
 * Verifica:
 *   - geração da sequência do 27 (111 passos, pico 9232);
 *   - busca por recorde em 1..1.000.000 com threads nativas do Windows,
 *     que deve achar n=837799 com 524 passos.
 * Imprime o resultado no console; termina com código 0 (sucesso) ou 1.
 */
#include <stdio.h>
#include <windows.h>
#include "collatz.h"

int main(void)
{
    int falhas = 0;

    uint64_t buf[4096];
    size_t len = 0;
    int ovf = 0;
    collatz_generate(27, buf, 4096, &len, &ovf);
    uint64_t maxv = 0;
    for (size_t i = 0; i < len; i++)
        if (buf[i] > maxv) maxv = buf[i];
    printf("27 -> passos=%llu pico=%llu\n",
           (unsigned long long)(len - 1), (unsigned long long)maxv);
    if (len - 1 != 111 || maxv != 9232) {
        printf("  FALHA no 27\n");
        falhas++;
    }

    CollatzRecord rec = {0, 0};
    volatile int cancel = 0;
    int rc = collatz_search(1, 1000000, collatz_cpu_count(), 0, 0, &rec,
                            NULL, NULL, &cancel);
    printf("busca 1..1e6 -> status=%d n=%llu passos=%llu (cpus=%u)\n",
           rc, (unsigned long long)rec.n, (unsigned long long)rec.steps,
           collatz_cpu_count());
    if (rec.n != 837799 || rec.steps != 524) {
        printf("  FALHA na busca\n");
        falhas++;
    }

    printf(falhas ? "%d FALHA(S)\n" : "TODOS OS TESTES PASSARAM\n", falhas);
    return falhas ? 1 : 0;
}
