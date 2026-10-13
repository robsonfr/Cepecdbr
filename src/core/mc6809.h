/*
 * mc6809.h - Emulacao da CPU Motorola MC6809E.
 */
#ifndef MC6809_H
#define MC6809_H

#include <stdint.h>

/* Bits do registrador CC */
#define CC_E 0x80
#define CC_F 0x40
#define CC_H 0x20
#define CC_I 0x10
#define CC_N 0x08
#define CC_Z 0x04
#define CC_V 0x02
#define CC_C 0x01

typedef uint8_t (*mc6809_read_fn)(void *ctx, uint16_t addr);
typedef void    (*mc6809_write_fn)(void *ctx, uint16_t addr, uint8_t val);

typedef struct mc6809 {
    /* registradores */
    uint8_t  a, b, dp, cc;
    uint16_t x, y, u, s, pc;

    /* barramento */
    void            *ctx;
    mc6809_read_fn   read;
    mc6809_write_fn  write;

    /* linhas de interrupcao (nivel; NMI e detectado por borda) */
    int nmi_line;
    int nmi_latched;
    int nmi_armed;    /* NMI so e aceita apos o primeiro carregamento de S */
    int firq_line;
    int irq_line;

    /* estados especiais */
    int cwai;         /* aguardando interrupcao apos CWAI (estado ja empilhado) */
    int sync;         /* aguardando interrupcao apos SYNC */

    uint64_t cycles;  /* total de ciclos executados */
} mc6809;

void mc6809_init(mc6809 *cpu, void *ctx, mc6809_read_fn rd, mc6809_write_fn wr);
void mc6809_reset(mc6809 *cpu);
/* Executa uma instrucao (ou atende uma interrupcao). Retorna ciclos gastos. */
int  mc6809_step(mc6809 *cpu);

void mc6809_set_nmi(mc6809 *cpu, int level);
void mc6809_set_firq(mc6809 *cpu, int level);
void mc6809_set_irq(mc6809 *cpu, int level);

#endif /* MC6809_H */
