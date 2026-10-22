/*
 * difftest.c - Teste diferencial da CPU 6809 do coco4k (assembly x86)
 * contra a CPU de referencia em C (src/core/mc6809.c).
 *
 * Para cada iteracao: registradores aleatorios, uma instrucao valida
 * aleatoria no PC, um passo nas duas CPUs; compara registradores, flags
 * e os 64K de memoria.  Ciclos nao sao comparados (o coco4k aproxima).
 *
 * Uso: difftest [iteracoes] [semente]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "mc6809.h"

/* estado do nucleo em assembly: 128 bytes antes da memoria */
#define R_B   (-32)
#define R_A   (-31)
#define R_X   (-30)
#define R_Y   (-28)
#define R_U   (-26)
#define R_S   (-24)
#define R_PC  (-22)
#define R_CC  (-20)
#define R_DP  (-19)
#define P0B_CR (-64 + 4 + 2)

extern void t_step(uint8_t *mem);

static uint8_t abuf[128 + 0x10000];
static uint8_t *amem = abuf + 128;
static uint8_t cmem[0x10000];

static uint8_t rd(void *c, uint16_t a) { (void)c; return cmem[a]; }
static void wr(void *c, uint16_t a, uint8_t v) { (void)c; cmem[a] = v; }

static uint16_t aw(int off) { return (uint16_t)(amem[off] | (amem[off + 1] << 8)); }
static void set_aw(int off, uint16_t v) { amem[off] = (uint8_t)v; amem[off + 1] = (uint8_t)(v >> 8); }

static uint32_t rng = 12345;
static uint32_t rnd(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

/* opcodes validos (documentados, ou nao documentados com o mesmo efeito
 * nos dois nucleos); exclui SYNC e CWAI (estados de espera) */
static int valid1(int op)
{
    static const uint8_t bad[] = { 0x02, 0x13, 0x14, 0x15, 0x18, 0x1B, 0x38, 0x3C, 0x3E,
        0x42, 0x4E, 0x52, 0x5E, 0x62, 0x72, 0x87, 0x8F, 0xC7, 0xCD, 0xCF, 0x10, 0x11,
        0x41, 0x45, 0x4B, 0x51, 0x55, 0x5B, 0x61, 0x65, 0x6B, 0x71, 0x75, 0x7B, 0x01, 0x05, 0x0B };
    size_t i;
    for (i = 0; i < sizeof(bad); i++)
        if (bad[i] == op) return 0;
    return 1;
}
static const uint8_t page2[] = { 0x21,0x22,0x23,0x24,0x25,0x26,0x27,0x28,0x29,0x2A,0x2B,0x2C,
    0x2D,0x2E,0x2F,0x3F,0x83,0x8C,0x8E,0x93,0x9C,0x9E,0x9F,0xA3,0xAC,0xAE,0xAF,0xB3,0xBC,
    0xBE,0xBF,0xCE,0xDE,0xDF,0xEE,0xEF,0xFE,0xFF };
static const uint8_t page3[] = { 0x3F,0x83,0x8C,0x93,0x9C,0xA3,0xAC,0xB3,0xBC };

static void dump(const char *who, mc6809 *c)
{
    printf("  %s: A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X PC=%04X DP=%02X CC=%02X\n",
           who, c->a, c->b, c->x, c->y, c->u, c->s, c->pc, c->dp, c->cc);
}

int main(int argc, char **argv)
{
    long iters = argc > 1 ? atol(argv[1]) : 1000000;
    long i, fails = 0;
    mc6809 c;
    if (argc > 2) rng = (uint32_t)atol(argv[2]);

    for (i = 0; i < 0x10000; i++) cmem[i] = (uint8_t)rnd();
    mc6809_init(&c, NULL, rd, wr);

    for (i = 0; i < iters && fails < 20; i++) {
        uint16_t pc = (uint16_t)(rnd() & 0xFFFF);
        int k, op;
        mc6809 before;

        /* instrucao aleatoria valida em PC (+ 4 bytes de operandos) */
        k = rnd() % 10;
        if (k == 0) {
            cmem[pc] = 0x10;
            cmem[(uint16_t)(pc + 1)] = page2[rnd() % sizeof(page2)];
        } else if (k == 1) {
            cmem[pc] = 0x11;
            cmem[(uint16_t)(pc + 1)] = page3[rnd() % sizeof(page3)];
        } else {
            do op = rnd() & 0xFF; while (!valid1(op));
            cmem[pc] = (uint8_t)op;
            cmem[(uint16_t)(pc + 1)] = (uint8_t)rnd();
        }
        for (k = 2; k < 6; k++) cmem[(uint16_t)(pc + k)] = (uint8_t)rnd();

        c.a = (uint8_t)rnd(); c.b = (uint8_t)rnd(); c.dp = (uint8_t)rnd();
        c.cc = (uint8_t)rnd() | 0x50;      /* I e F mascaradas */
        c.x = (uint16_t)rnd(); c.y = (uint16_t)rnd(); c.u = (uint16_t)rnd();
        c.s = (uint16_t)rnd(); c.pc = pc;
        c.cwai = c.sync = 0; c.nmi_latched = 0; c.irq_line = c.firq_line = 0;

        memcpy(amem, cmem, 0x10000);
        memset(abuf, 0, 128);
        amem[R_A] = c.a; amem[R_B] = c.b; amem[R_DP] = c.dp; amem[R_CC] = c.cc;
        set_aw(R_X, c.x); set_aw(R_Y, c.y); set_aw(R_U, c.u); set_aw(R_S, c.s); set_aw(R_PC, c.pc);
        before = c;

        mc6809_step(&c);
        t_step(amem);

        if (amem[R_A] != c.a || amem[R_B] != c.b || amem[R_DP] != c.dp || amem[R_CC] != c.cc ||
            aw(R_X) != c.x || aw(R_Y) != c.y || aw(R_U) != c.u || aw(R_S) != c.s ||
            aw(R_PC) != c.pc || memcmp(amem, cmem, 0x10000) != 0) {
            mc6809 a = c;
            int m;
            fails++;
            a.a = amem[R_A]; a.b = amem[R_B]; a.dp = amem[R_DP]; a.cc = amem[R_CC];
            a.x = aw(R_X); a.y = aw(R_Y); a.u = aw(R_U); a.s = aw(R_S); a.pc = aw(R_PC);
            printf("DIFERENCA #%ld: bytes %02X %02X %02X %02X %02X\n", i,
                   cmem[pc], cmem[(uint16_t)(pc+1)], cmem[(uint16_t)(pc+2)],
                   cmem[(uint16_t)(pc+3)], cmem[(uint16_t)(pc+4)]);
            dump("antes", &before);
            dump("C    ", &c);
            dump("asm  ", &a);
            for (m = 0; m < 0x10000; m++)
                if (amem[m] != cmem[m])
                    printf("  mem[%04X]: C=%02X asm=%02X\n", m, cmem[m], amem[m]);
        }
        /* sincroniza a memoria para a proxima iteracao */
        memcpy(cmem, amem, 0); 
    }
    printf("%ld instrucoes testadas, %ld diferencas\n", i, fails);
    return fails ? 1 : 0;
}
