/*
 * collatz.h - Motor de cálculo da sequência de Collatz.
 *
 * Objetivo: gerar a sequência de Collatz para qualquer número de partida
 * com desempenho alto, aproveitando threads e memoização.
 */
#ifndef COLLATZ_H
#define COLLATZ_H

#include <stddef.h>
#include <stdint.h>

/* Limite padrão de passos para a busca de recordes, para evitar
 * varreduras muito longas em faixas enormes. */
#define COLLATZ_DEFAULT_MAX_STEPS 100000000ULL

/* Um resultado de recorde: qual número gerou a maior sequência
 * em uma faixa de busca. */
typedef struct {
    uint64_t n;     /* número de partida do recorde            */
    uint64_t steps; /* quantidade de passos até chegar em 1    */
} CollatzRecord;

/*
 * Gera a sequência de Collatz para um número de partida arbitrário.
 *
 * A sequência inclui o próprio número de partida e termina em 1.
 * Usa aritmética de 128 bits para detectar overflow com segurança.
 *
 * start          - número inicial (>= 1)
 * out            - buffer que receberá os valores (pode ser NULL apenas
 *                  quando capacity == 0, para apenas medir o tamanho)
 * capacity       - tamanho máximo do buffer
 * out_len        - recebe a quantidade de valores gerados
 * overflow       - recebe 1 se algum valor ultrapassou 64 bits (não cabe
 *                  no uint64_t). Nesse caso o cálculo para no ponto seguro.
 *
 * Retorna 0 em caso de sucesso e -1 em erro de argumento.
 */
int collatz_generate(uint64_t start, uint64_t *out, size_t capacity,
                     size_t *out_len, int *overflow);

/*
 * Calcula apenas a quantidade de passos até 1, sem materializar a sequência.
 * É a via rápida usada na busca por recordes.
 *
 * Retorna o número de passos, ou UINT64_MAX em caso de argumento inválido
 * ou overflow de 64 bits.
 */
uint64_t collatz_steps(uint64_t start);

/*
 * Busca o número de partida que gera a sequência mais longa em [start, end].
 *
 * Usa várias threads (n_threads) e memoização para acelerar.
 * Regras de throttling:
 *   - max_steps > 0: para assim que qualquer número precisar de mais
 *     de max_steps passos (proteção contra números gigantes).
 *   - time_budget_ms > 0: para quando o tempo de parede exceder esse valor.
 * Qualquer um pode ser 0 para desativar aquele limite.
 *
 * progress    - callback opcional chamado periodicamente com uma fração
 *               estimada de conclusão, de 0.0 a 1.0.
 * progress_ud - argumento repassado ao callback.
 * canceled    - flag opcional observada a cada iteração. Se virar 1,
 *               a busca encerra o quanto antes (resposta ao botão Cancelar).
 *
 * Retorna 0 em sucesso, 1 se cancelado e -1 em erro de argumento.
 * O recorde é escrito em *record.
 */
int collatz_search(uint64_t start, uint64_t end, unsigned n_threads,
                   uint64_t max_steps, uint64_t time_budget_ms,
                   CollatzRecord *record,
                   void (*progress)(double fraction, void *ud), void *progress_ud,
                   volatile int *canceled);

/* Quantidade de núcleos lógicos disponíveis. */
unsigned collatz_cpu_count(void);

#endif /* COLLATZ_H */
