/*
 * test_cpu.c - Testes unitarios da CPU MC6809 com programas montados a mao.
 */
#include <stdio.h>
#include <string.h>

#include "mc6809.h"

static uint8_t mem[0x10000];
static int failures, checks;

static uint8_t rd(void *ctx, uint16_t a) { (void)ctx; return mem[a]; }
static void wr(void *ctx, uint16_t a, uint8_t v) { (void)ctx; mem[a] = v; }

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FALHA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void setup(mc6809 *cpu, uint16_t org, const uint8_t *code, size_t len)
{
    memset(mem, 0, sizeof(mem));
    memcpy(&mem[org], code, len);
    mem[0xfffe] = (uint8_t)(org >> 8);
    mem[0xffff] = (uint8_t)org;
    mc6809_init(cpu, NULL, rd, wr);
}

static int run(mc6809 *cpu, int n)
{
    int total = 0;
    while (n-- > 0)
        total += mc6809_step(cpu);
    return total;
}

static void test_basic_arith(void)
{
    mc6809 c;
    static const uint8_t p1[] = { 0x86, 0x12, 0xc6, 0x34, 0xc3, 0x11, 0x11 };
    static const uint8_t p2[] = { 0x86, 0x7f, 0x8b, 0x01 };
    static const uint8_t p3[] = { 0x86, 0x00, 0x80, 0x01 };
    static const uint8_t p4[] = { 0x86, 0x19, 0x8b, 0x28, 0x19 };
    static const uint8_t p5[] = { 0x86, 0x0c, 0xc6, 0x0d, 0x3d };
    static const uint8_t p6[] = { 0x86, 0x80, 0x40 };
    static const uint8_t p7[] = { 0x86, 0xff, 0x8b, 0x01, 0x86, 0x10, 0x89, 0x05, 0x82, 0x01 };

    setup(&c, 0x100, p1, sizeof(p1)); run(&c, 3);
    CHECK(c.a == 0x23 && c.b == 0x45, "ADDD: D=%02X%02X", c.a, c.b);

    setup(&c, 0x100, p2, sizeof(p2)); run(&c, 2);
    CHECK(c.a == 0x80, "ADDA resultado %02X", c.a);
    CHECK((c.cc & (CC_V | CC_N | CC_H)) == (CC_V | CC_N | CC_H) && !(c.cc & (CC_Z | CC_C)),
          "ADDA flags CC=%02X", c.cc);

    setup(&c, 0x100, p3, sizeof(p3)); run(&c, 2);
    CHECK(c.a == 0xff && (c.cc & CC_C) && (c.cc & CC_N) && !(c.cc & CC_V), "SUBA: A=%02X CC=%02X", c.a, c.cc);

    setup(&c, 0x100, p4, sizeof(p4)); run(&c, 3);
    CHECK(c.a == 0x47, "DAA: A=%02X", c.a);

    setup(&c, 0x100, p5, sizeof(p5)); run(&c, 3);
    CHECK(c.a == 0x00 && c.b == 0x9c && (c.cc & CC_C), "MUL: D=%02X%02X CC=%02X", c.a, c.b, c.cc);

    setup(&c, 0x100, p6, sizeof(p6)); run(&c, 2);
    CHECK(c.a == 0x80 && (c.cc & CC_V) && (c.cc & CC_C), "NEGA: A=%02X CC=%02X", c.a, c.cc);

    /* ADDA gera carry; ADCA usa; SBCA */
    setup(&c, 0x100, p7, sizeof(p7)); run(&c, 2);
    CHECK(c.a == 0 && (c.cc & CC_C) && (c.cc & CC_Z), "ADDA carry: A=%02X CC=%02X", c.a, c.cc);
    run(&c, 2);
    CHECK(c.a == 0x16 && !(c.cc & CC_C), "ADCA com carry (LDA preserva C): A=%02X", c.a);
    run(&c, 1);
    CHECK(c.a == 0x15, "SBCA: A=%02X", c.a);
}

static void test_shifts(void)
{
    mc6809 c;
    static const uint8_t p[] = { 0x86, 0x81, 0x48, 0x49, 0x44, 0x86, 0x81, 0x47, 0x46 };
    setup(&c, 0x100, p, sizeof(p));
    run(&c, 2);
    CHECK(c.a == 0x02 && (c.cc & CC_C) && (c.cc & CC_V), "ASLA: A=%02X CC=%02X", c.a, c.cc);
    run(&c, 1);
    CHECK(c.a == 0x05 && !(c.cc & CC_C), "ROLA: A=%02X CC=%02X", c.a, c.cc);
    run(&c, 1);
    CHECK(c.a == 0x02 && (c.cc & CC_C), "LSRA: A=%02X CC=%02X", c.a, c.cc);
    run(&c, 2);
    CHECK(c.a == 0xc0 && (c.cc & CC_C) && (c.cc & CC_N), "ASRA: A=%02X CC=%02X", c.a, c.cc);
    run(&c, 1);
    CHECK(c.a == 0xe0 && !(c.cc & CC_C), "RORA: A=%02X CC=%02X", c.a, c.cc);
}

static void test_loop(void)
{
    mc6809 c;
    /* LDB #10; CLRA; loop: ADDA #3; DECB; BNE loop */
    static const uint8_t p[] = { 0xc6, 0x0a, 0x4f, 0x8b, 0x03, 0x5a, 0x26, 0xfb };
    int cyc;
    setup(&c, 0x100, p, sizeof(p));
    cyc = run(&c, 2 + 30);
    CHECK(c.a == 30 && c.b == 0 && c.pc == 0x108, "laco: A=%d B=%d PC=%04X", c.a, c.b, c.pc);
    CHECK(cyc == 2 + 2 + 10 * (2 + 2 + 3), "ciclos do laco: %d", cyc);
}

static void test_stack(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x10, 0xce, 0x80, 0x00,     /* LDS #$8000 */
        0x8e, 0x12, 0x34,           /* LDX #$1234 */
        0x86, 0x56,                 /* LDA #$56 */
        0x34, 0x12,                 /* PSHS A,X */
        0x4f,                       /* CLRA */
        0x8e, 0x00, 0x00,           /* LDX #0 */
        0x35, 0x12                  /* PULS A,X */
    };
    int cyc;
    setup(&c, 0x100, p, sizeof(p));
    run(&c, 3);
    cyc = run(&c, 1);
    CHECK(cyc == 8, "PSHS A,X ciclos: %d", cyc);
    CHECK(c.s == 0x7ffd && mem[0x7ffd] == 0x56 && mem[0x7ffe] == 0x12 && mem[0x7fff] == 0x34,
          "PSHS: S=%04X mem=%02X %02X %02X", c.s, mem[0x7ffd], mem[0x7ffe], mem[0x7fff]);
    run(&c, 3);
    CHECK(c.a == 0x56 && c.x == 0x1234 && c.s == 0x8000, "PULS: A=%02X X=%04X S=%04X", c.a, c.x, c.s);
}

static void test_subroutine(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x10, 0xce, 0x80, 0x00,     /* 0100 LDS #$8000 */
        0x8d, 0x03,                 /* 0104 BSR $0109 */
        0x86, 0xaa,                 /* 0106 LDA #$AA */
        0x12,                       /* 0108 NOP */
        0xc6, 0xbb,                 /* 0109 LDB #$BB */
        0x39                        /* 010B RTS */
    };
    setup(&c, 0x100, p, sizeof(p));
    run(&c, 5);
    CHECK(c.a == 0xaa && c.b == 0xbb && c.pc == 0x108 && c.s == 0x8000,
          "BSR/RTS: A=%02X B=%02X PC=%04X S=%04X", c.a, c.b, c.pc, c.s);
}

static void test_indexed(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x8e, 0x20, 0x00,           /* LDX #$2000 */
        0x86, 0x11, 0xa7, 0x80,     /* LDA #$11; STA ,X+ */
        0x86, 0x22, 0xa7, 0x80,     /* LDA #$22; STA ,X+ */
        0xa6, 0x1e,                 /* LDA -2,X */
        0xa6, 0x9f, 0x30, 0x00,     /* LDA [$3000] */
        0x10, 0x8e, 0x20, 0x00,     /* LDY #$2000 */
        0xe6, 0xa1,                 /* LDB ,Y++ */
        0xe6, 0xa4,                 /* LDB ,Y */
        0xa6, 0xa2,                 /* LDA ,-Y */
        0x86, 0x01, 0xe6, 0xa6,     /* LDA #1; LDB A,Y */
        0x8c, 0x20, 0x02,           /* CMPX #$2002 */
        0xa6, 0x8c, 0xfc,           /* LDA -4,PCR -> byte $02 de CMPX */
    };
    setup(&c, 0x100, p, sizeof(p));
    mem[0x3000] = 0x20; mem[0x3001] = 0x01;
    run(&c, 5);
    CHECK(c.x == 0x2002 && mem[0x2000] == 0x11 && mem[0x2001] == 0x22, "STA ,X+");
    run(&c, 1);
    CHECK(c.a == 0x11, "LDA -2,X: %02X", c.a);
    run(&c, 1);
    CHECK(c.a == 0x22, "LDA [$3000]: %02X", c.a);
    run(&c, 2);
    CHECK(c.b == 0x11 && c.y == 0x2002, "LDB ,Y++: B=%02X Y=%04X", c.b, c.y);
    run(&c, 1);
    CHECK(c.b == 0x00, "LDB ,Y: %02X", c.b);
    run(&c, 1);
    CHECK(c.a == 0x22 && c.y == 0x2001, "LDA ,-Y: A=%02X Y=%04X", c.a, c.y);
    run(&c, 2);
    CHECK(c.b == 0x00, "LDB A,Y: %02X", c.b);
    run(&c, 1);
    CHECK(c.cc & CC_Z, "CMPX igual: CC=%02X", c.cc);
    run(&c, 1);
    CHECK(c.a == 0x02, "LDA n,PCR: %02X", c.a);
}

static void test_long_branch_and_pages(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x8e, 0x10, 0x00,           /* LDX #$1000 */
        0x8c, 0x10, 0x00,           /* CMPX #$1000 */
        0x10, 0x27, 0x00, 0x02,     /* LBEQ +2 */
        0x86, 0x01,                 /* LDA #1 (pulado) */
        0x86, 0x02,                 /* LDA #2 */
        0xce, 0x00, 0x10,           /* LDU #$10 */
        0x11, 0x83, 0x00, 0x20,     /* CMPU #$20 */
        0x10, 0x8e, 0xbe, 0xef,     /* LDY #$BEEF */
        0x10, 0x8c, 0xbe, 0xef,     /* CMPY #$BEEF */
    };
    int cyc;
    setup(&c, 0x100, p, sizeof(p));
    run(&c, 2);
    cyc = run(&c, 1);
    CHECK(cyc == 6, "LBEQ tomado ciclos: %d", cyc);
    run(&c, 1);
    CHECK(c.a == 0x02, "LBEQ: A=%02X", c.a);
    run(&c, 2);
    CHECK((c.cc & CC_C) && (c.cc & CC_N), "CMPU: CC=%02X", c.cc);
    run(&c, 2);
    CHECK(c.y == 0xbeef && (c.cc & CC_Z), "LDY/CMPY: Y=%04X CC=%02X", c.y, c.cc);
}

static void test_transfer(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x86, 0x01, 0xc6, 0x02,     /* LDA #1; LDB #2 */
        0x1e, 0x89,                 /* EXG A,B */
        0x8e, 0x12, 0x34,           /* LDX #$1234 */
        0x1f, 0x12,                 /* TFR X,Y */
        0xc6, 0x80, 0x1d,           /* LDB #$80; SEX */
        0x8e, 0x10, 0x00,           /* LDX #$1000 */
        0x30, 0x05,                 /* LEAX 5,X */
        0xcc, 0x00, 0x10,           /* LDD #$0010 */
        0x31, 0x8b,                 /* LEAY D,X */
        0x86, 0x20, 0x1f, 0x8b,     /* LDA #$20; TFR A,DP */
        0xcc, 0xbe, 0xef,           /* LDD #$BEEF */
        0xdd, 0x10,                 /* STD <$10 */
        0x3a,                       /* ABX */
    };
    setup(&c, 0x100, p, sizeof(p));
    run(&c, 3);
    CHECK(c.a == 2 && c.b == 1, "EXG: A=%02X B=%02X", c.a, c.b);
    run(&c, 2);
    CHECK(c.y == 0x1234, "TFR X,Y: %04X", c.y);
    run(&c, 2);
    CHECK(c.a == 0xff && (c.cc & CC_N), "SEX: A=%02X", c.a);
    run(&c, 2);
    CHECK(c.x == 0x1005, "LEAX 5,X: %04X", c.x);
    run(&c, 2);
    CHECK(c.y == 0x1015, "LEAY D,X: %04X", c.y);
    run(&c, 2);
    CHECK(c.dp == 0x20, "TFR A,DP: %02X", c.dp);
    run(&c, 2);
    CHECK(mem[0x2010] == 0xbe && mem[0x2011] == 0xef, "STD direto");
    run(&c, 1);
    CHECK(c.x == 0x1005 + 0xef, "ABX: %04X", c.x);
}

static void test_interrupts(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x10, 0xce, 0x80, 0x00,     /* LDS #$8000 */
        0x1c, 0xef,                 /* ANDCC #$EF (libera IRQ) */
        0x12, 0x12,                 /* NOP; NOP */
        0x3f,                       /* SWI */
        0x12
    };
    static const uint8_t isr[] = { 0xc6, 0x77, 0x3b };          /* LDB #$77; RTI */
    static const uint8_t swi[] = { 0x86, 0x55, 0x3b };          /* LDA #$55; RTI */
    static const uint8_t fisr[] = { 0x4c, 0x3b };               /* INCA; RTI */
    int cyc;

    setup(&c, 0x100, p, sizeof(p));
    memcpy(&mem[0x200], isr, sizeof(isr));
    memcpy(&mem[0x300], swi, sizeof(swi));
    memcpy(&mem[0x400], fisr, sizeof(fisr));
    mem[0xfff8] = 0x02; mem[0xfff9] = 0x00;
    mem[0xfffa] = 0x03; mem[0xfffb] = 0x00;
    mem[0xfff6] = 0x04; mem[0xfff7] = 0x00;

    run(&c, 2);
    mc6809_set_irq(&c, 1);
    cyc = run(&c, 1);
    CHECK(cyc == 19 && c.pc == 0x200 && (c.cc & CC_I) && c.s == 0x8000 - 12,
          "IRQ: ciclos=%d PC=%04X CC=%02X S=%04X", cyc, c.pc, c.cc, c.s);
    mc6809_set_irq(&c, 0);
    run(&c, 1);
    CHECK(c.b == 0x77, "ISR: B=%02X", c.b);
    cyc = run(&c, 1);
    /* RTI com E=1 restaura todos os registradores (B volta a 0) */
    CHECK(c.b == 0x00 && c.pc == 0x106 && cyc == 15 && !(c.cc & CC_I),
          "RTI: B=%02X PC=%04X ciclos=%d", c.b, c.pc, cyc);
    run(&c, 2);
    run(&c, 1);   /* SWI */
    CHECK(c.pc == 0x300 && (c.cc & CC_F) && (c.cc & CC_I), "SWI: PC=%04X", c.pc);
    run(&c, 1);
    CHECK(c.a == 0x55, "SWI handler: A=%02X", c.a);
    run(&c, 1);
    CHECK(c.a == 0x00 && c.pc == 0x109, "SWI/RTI: A=%02X PC=%04X", c.a, c.pc);

    /* FIRQ empilha somente PC e CC */
    c.cc &= (uint8_t)~CC_F;
    mc6809_set_firq(&c, 1);
    cyc = run(&c, 1);
    CHECK(cyc == 10 && c.pc == 0x400 && c.s == 0x8000 - 3, "FIRQ: ciclos=%d PC=%04X S=%04X", cyc, c.pc, c.s);
    mc6809_set_firq(&c, 0);
    run(&c, 1);
    cyc = run(&c, 1);
    /* FIRQ nao empilha A: o INCA permanece */
    CHECK(cyc == 6 && c.a == 0x01 && c.pc == 0x109, "RTI rapido: ciclos=%d A=%02X PC=%04X", cyc, c.a, c.pc);

    /* NMI so e aceita depois de S ter sido carregado */
    mc6809_set_nmi(&c, 1);
    mc6809_set_nmi(&c, 0);
    mem[0xfffc] = 0x02; mem[0xfffd] = 0x00;
    cyc = run(&c, 1);
    CHECK(c.pc == 0x200 && cyc == 19, "NMI: PC=%04X ciclos=%d", c.pc, cyc);
}

static void test_cycles(void)
{
    mc6809 c;
    static const uint8_t p[] = {
        0x86, 0x00,                 /* LDA #0        2 */
        0xb6, 0x12, 0x34,           /* LDA $1234     5 */
        0x10, 0xce, 0x80, 0x00,     /* LDS #$8000    4 */
        0xbd, 0x02, 0x00,           /* JSR $0200     8 */
    };
    setup(&c, 0x100, p, sizeof(p));
    mem[0x200] = 0x39;
    CHECK(run(&c, 1) == 2, "LDA imediato");
    CHECK(run(&c, 1) == 5, "LDA estendido");
    CHECK(run(&c, 1) == 4, "LDS imediato");
    CHECK(run(&c, 1) == 8, "JSR estendido");
    CHECK(run(&c, 1) == 5, "RTS");
}

int main(void)
{
    test_basic_arith();
    test_shifts();
    test_loop();
    test_stack();
    test_subroutine();
    test_indexed();
    test_long_branch_and_pages();
    test_transfer();
    test_interrupts();
    test_cycles();
    printf("%d verificacoes, %d falhas\n", checks, failures);
    return failures ? 1 : 0;
}
