/*
 * mc6821.h - Peripheral Interface Adapter (PIA) Motorola MC6821.
 */
#ifndef MC6821_H
#define MC6821_H

#include <stdint.h>

typedef struct mc6821_port {
    uint8_t out;        /* registrador de saida (ORx) */
    uint8_t ddr;        /* direcao: 1 = saida */
    uint8_t cr;         /* registrador de controle (CRx) */
    uint8_t in;         /* nivel dos pinos de entrada, fornecido externamente */
    int     c1;         /* nivel atual de Cx1 */
    int     c2;         /* nivel atual de Cx2 (entrada) */
    int     irq1;       /* flag IRQx1 */
    int     irq2;       /* flag IRQx2 */
    int     c2_out;     /* nivel de Cx2 quando configurado como saida */
} mc6821_port;

typedef struct mc6821 {
    mc6821_port a, b;
    void *ctx;
    /* chamado antes de ler um registrador de dados (para atualizar 'in') */
    void (*pre_read)(void *ctx, struct mc6821 *pia, int port);
    /* chamado apos qualquer mudanca nas saidas (dados, DDR ou CA2/CB2) */
    void (*output_changed)(void *ctx, struct mc6821 *pia, int port);
    /* chamado quando a saida IRQ muda */
    void (*irq_changed)(void *ctx, struct mc6821 *pia);
} mc6821;

void    mc6821_reset(mc6821 *pia);
uint8_t mc6821_read(mc6821 *pia, int reg);
void    mc6821_write(mc6821 *pia, int reg, uint8_t val);
void    mc6821_set_c1(mc6821 *pia, int port, int level);
void    mc6821_set_c2(mc6821 *pia, int port, int level);

/* valor visto nos pinos de saida (bits de entrada lidos como 1: pull-up) */
uint8_t mc6821_port_value(const mc6821_port *p);
int     mc6821_irq_a(const mc6821 *pia);
int     mc6821_irq_b(const mc6821 *pia);
/* nivel efetivo de Cx2 (como saida) */
int     mc6821_c2(const mc6821_port *p);

#endif /* MC6821_H */
