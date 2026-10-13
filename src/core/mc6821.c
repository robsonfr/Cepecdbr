/*
 * mc6821.c - Peripheral Interface Adapter (PIA) Motorola MC6821.
 *
 * CRx:  b0 = habilita IRQ de Cx1      b1 = borda ativa de Cx1 (1 = subida)
 *       b2 = 0: acessa DDR, 1: dados  b3-b5 = controle de Cx2
 *       b6 = flag IRQx2 (somente leitura)  b7 = flag IRQx1 (somente leitura)
 */
#include "mc6821.h"

static int port_irq(const mc6821_port *p)
{
    int irq = p->irq1 && (p->cr & 0x01);
    if (!(p->cr & 0x20))                 /* Cx2 como entrada */
        irq |= p->irq2 && (p->cr & 0x08);
    return irq;
}

int mc6821_irq_a(const mc6821 *pia) { return port_irq(&pia->a); }
int mc6821_irq_b(const mc6821 *pia) { return port_irq(&pia->b); }

int mc6821_c2(const mc6821_port *p)
{
    if (p->cr & 0x20)
        return p->c2_out;
    return 1;   /* entrada: considerado em nivel alto (pull-up) */
}

uint8_t mc6821_port_value(const mc6821_port *p)
{
    return (uint8_t)((p->out & p->ddr) | ~p->ddr);
}

static void notify_irq(mc6821 *pia)
{
    if (pia->irq_changed)
        pia->irq_changed(pia->ctx, pia);
}

static void notify_out(mc6821 *pia, int port)
{
    if (pia->output_changed)
        pia->output_changed(pia->ctx, pia, port);
}

void mc6821_reset(mc6821 *pia)
{
    mc6821_port *ports[2];
    int i;
    ports[0] = &pia->a;
    ports[1] = &pia->b;
    for (i = 0; i < 2; i++) {
        mc6821_port *p = ports[i];
        p->out = 0;
        p->ddr = 0;
        p->cr = 0;
        p->irq1 = p->irq2 = 0;
        p->c2_out = 1;
        p->in = 0xff;
        p->c1 = 1;
        p->c2 = 1;
    }
    notify_out(pia, 0);
    notify_out(pia, 1);
    notify_irq(pia);
}

uint8_t mc6821_read(mc6821 *pia, int reg)
{
    mc6821_port *p = (reg & 2) ? &pia->b : &pia->a;
    int port = (reg & 2) ? 1 : 0;

    if (reg & 1) {
        return (uint8_t)((p->cr & 0x3f) | (p->irq1 ? 0x80 : 0) | (p->irq2 ? 0x40 : 0));
    }
    if (!(p->cr & 0x04))
        return p->ddr;

    if (pia->pre_read)
        pia->pre_read(pia->ctx, pia, port);

    /* leitura dos dados limpa os flags de interrupcao */
    if (p->irq1 || p->irq2) {
        p->irq1 = p->irq2 = 0;
        notify_irq(pia);
    }
    if (port == 0) {
        /* porta A: le os pinos; saidas sao "puxadas" pela carga externa */
        return (uint8_t)(p->in & ((p->out & p->ddr) | ~p->ddr));
    }
    /* porta B: bits de saida vem do registrador */
    return (uint8_t)((p->out & p->ddr) | (p->in & ~p->ddr));
}

void mc6821_write(mc6821 *pia, int reg, uint8_t val)
{
    mc6821_port *p = (reg & 2) ? &pia->b : &pia->a;
    int port = (reg & 2) ? 1 : 0;

    if (reg & 1) {
        uint8_t old = p->cr;
        p->cr = val & 0x3f;
        if ((val & 0x30) == 0x30) {          /* Cx2 saida manual */
            p->c2_out = (val & 0x08) ? 1 : 0;
        } else if ((val & 0x30) == 0x20) {   /* Cx2 saida com handshake */
            p->c2_out = 1;
        }
        if (old != p->cr) notify_out(pia, port);
        notify_irq(pia);
        return;
    }
    if (p->cr & 0x04) {
        p->out = val;
        /* modo handshake na porta B: escrita baixa CB2 */
        if (port == 1 && (p->cr & 0x38) == 0x20)
            p->c2_out = 0;
    } else {
        p->ddr = val;
    }
    notify_out(pia, port);
}

void mc6821_set_c1(mc6821 *pia, int port, int level)
{
    mc6821_port *p = port ? &pia->b : &pia->a;
    level = level ? 1 : 0;
    if (level != p->c1) {
        int rising = level && !p->c1;
        int active_rising = (p->cr & 0x02) != 0;
        p->c1 = level;
        if (rising == active_rising) {
            p->irq1 = 1;
            /* handshake: borda ativa em C1 libera C2 */
            if ((p->cr & 0x30) == 0x20 && !(p->cr & 0x08)) {
                p->c2_out = 1;
                notify_out(pia, port);
            }
            notify_irq(pia);
        }
    }
}

void mc6821_set_c2(mc6821 *pia, int port, int level)
{
    mc6821_port *p = port ? &pia->b : &pia->a;
    level = level ? 1 : 0;
    if (level != p->c2) {
        int rising = level && !p->c2;
        int active_rising = (p->cr & 0x10) != 0;
        p->c2 = level;
        if (!(p->cr & 0x20) && rising == active_rising) {
            p->irq2 = 1;
            notify_irq(pia);
        }
    }
}
