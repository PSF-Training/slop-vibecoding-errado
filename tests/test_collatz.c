/* Teste rápido do motor de Collatz, sem interface gráfica. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "collatz.h"

int main(void)
{
    int failures = 0;

    /* Caso conhecido: 27 tem 111 passos e pico 9232. */
    uint64_t buf[4096];
    size_t len = 0;
    int ovf = 0;
    if (collatz_generate(27, buf, 4096, &len, &ovf) != 0) {
        printf("FALHA: collatz_generate(27) retornou erro\n");
        failures++;
    } else {
        uint64_t maxv = 0;
        for (size_t i = 0; i < len; i++)
            if (buf[i] > maxv) maxv = buf[i];
        printf("27 -> passos=%zu, ultimo=%llu, pico=%llu\n",
               len - 1, (unsigned long long)buf[len - 1],
               (unsigned long long)maxv);
        if (len - 1 != 111) { printf("  FALHA: esperado 111 passos\n"); failures++; }
        if (buf[len - 1] != 1) { printf("  FALHA: deveria terminar em 1\n"); failures++; }
        if (maxv != 9232) { printf("  FALHA: pico esperado 9232\n"); failures++; }
    }

    /* 1 -> sequência {1}, 0 passos. */
    size_t l1 = 0; int o1 = 0;
    collatz_generate(1, buf, 4096, &l1, &o1);
    printf("1 -> tamanho=%zu\n", l1);

    /* Busca em 1..1000000: recorde conhecido em 837799 com 524 passos. */
    CollatzRecord rec = {0, 0};
    volatile int cancel = 0;
    int rc = collatz_search(1, 1000000, collatz_cpu_count(), 0, 0, &rec,
                            NULL, NULL, &cancel);
    printf("Busca 1..1e6 -> status=%d, recorde n=%llu passos=%llu\n",
           rc, (unsigned long long)rec.n, (unsigned long long)rec.steps);
    if (rec.n != 837799 || rec.steps != 524) {
        printf("  FALHA: esperado n=837799, passos=524\n");
        failures++;
    }

    printf(failures ? "\n%d FALHA(S)\n" : "\nTodos os testes passaram.\n", failures);
    return failures ? 1 : 0;
}
