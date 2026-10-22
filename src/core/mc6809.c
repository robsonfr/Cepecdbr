/*
 * mc6809.c - Emulacao da CPU Motorola MC6809E.
 *
 * Implementa todo o conjunto de instrucoes documentado (paginas 1, 2 e 3),
 * os modos de enderecamento indexados, interrupcoes NMI/FIRQ/IRQ, SWI1-3,
 * CWAI e SYNC, com contagem de ciclos.  Alguns opcodes nao documentados
 * comuns (aliases de NEG/LSR/DEC) tambem sao tratados.
 */
#include "mc6809.h"

#define RD(a)    cpu->read(cpu->ctx, (uint16_t)(a))
#define WR(a, v) cpu->write(cpu->ctx, (uint16_t)(a), (uint8_t)(v))

#define GET_D()  ((uint16_t)((cpu->a << 8) | cpu->b))
#define SET_D(v) do { uint16_t _v = (uint16_t)(v); cpu->a = (uint8_t)(_v >> 8); cpu->b = (uint8_t)_v; } while (0)

/* ------------------------------------------------------------------------ */
/* Acesso a memoria                                                         */
/* ------------------------------------------------------------------------ */

static uint16_t rd16(mc6809 *cpu, uint16_t addr)
{
    uint8_t hi = RD(addr);
    uint8_t lo = RD((uint16_t)(addr + 1));
    return (uint16_t)((hi << 8) | lo);
}

static void wr16(mc6809 *cpu, uint16_t addr, uint16_t v)
{
    WR(addr, v >> 8);
    WR((uint16_t)(addr + 1), v & 0xff);
}

static uint8_t fetch8(mc6809 *cpu)
{
    return RD(cpu->pc++);
}

static uint16_t fetch16(mc6809 *cpu)
{
    uint16_t v = rd16(cpu, cpu->pc);
    cpu->pc += 2;
    return v;
}

static void push8(mc6809 *cpu, uint16_t *sp, uint8_t v)
{
    (*sp)--;
    WR(*sp, v);
}

static void push16(mc6809 *cpu, uint16_t *sp, uint16_t v)
{
    push8(cpu, sp, v & 0xff);
    push8(cpu, sp, v >> 8);
}

static uint8_t pull8(mc6809 *cpu, uint16_t *sp)
{
    uint8_t v = RD(*sp);
    (*sp)++;
    return v;
}

static uint16_t pull16(mc6809 *cpu, uint16_t *sp)
{
    uint16_t hi = pull8(cpu, sp);
    uint16_t lo = pull8(cpu, sp);
    return (uint16_t)((hi << 8) | lo);
}

/* ------------------------------------------------------------------------ */
/* Flags e ALU                                                              */
/* ------------------------------------------------------------------------ */

static void set_nz8(mc6809 *cpu, uint8_t r)
{
    cpu->cc &= (uint8_t)~(CC_N | CC_Z);
    if (r & 0x80) cpu->cc |= CC_N;
    if (r == 0)   cpu->cc |= CC_Z;
}

static void set_nz16(mc6809 *cpu, uint16_t r)
{
    cpu->cc &= (uint8_t)~(CC_N | CC_Z);
    if (r & 0x8000) cpu->cc |= CC_N;
    if (r == 0)     cpu->cc |= CC_Z;
}

static uint8_t alu_add8(mc6809 *cpu, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a + b + (carry ? 1 : 0);
    cpu->cc &= (uint8_t)~(CC_H | CC_V | CC_C);
    if ((a ^ b ^ r) & 0x10)        cpu->cc |= CC_H;
    if ((a ^ r) & (b ^ r) & 0x80)  cpu->cc |= CC_V;
    if (r & 0x100)                 cpu->cc |= CC_C;
    set_nz8(cpu, (uint8_t)r);
    return (uint8_t)r;
}

static uint8_t alu_sub8(mc6809 *cpu, uint8_t a, uint8_t b, int carry)
{
    unsigned r = (unsigned)a - b - (carry ? 1 : 0);
    cpu->cc &= (uint8_t)~(CC_V | CC_C);
    if ((a ^ b) & (a ^ r) & 0x80)  cpu->cc |= CC_V;
    if (r & 0x100)                 cpu->cc |= CC_C;
    set_nz8(cpu, (uint8_t)r);
    return (uint8_t)r;
}

static uint16_t alu_add16(mc6809 *cpu, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a + b;
    cpu->cc &= (uint8_t)~(CC_V | CC_C);
    if ((a ^ r) & (b ^ r) & 0x8000) cpu->cc |= CC_V;
    if (r & 0x10000)                cpu->cc |= CC_C;
    set_nz16(cpu, (uint16_t)r);
    return (uint16_t)r;
}

static uint16_t alu_sub16(mc6809 *cpu, uint16_t a, uint16_t b)
{
    uint32_t r = (uint32_t)a - b;
    cpu->cc &= (uint8_t)~(CC_V | CC_C);
    if ((a ^ b) & (a ^ r) & 0x8000) cpu->cc |= CC_V;
    if (r & 0x10000)                cpu->cc |= CC_C;
    set_nz16(cpu, (uint16_t)r);
    return (uint16_t)r;
}

static void ld_flags8(mc6809 *cpu, uint8_t v)
{
    cpu->cc &= (uint8_t)~CC_V;
    set_nz8(cpu, v);
}

static void ld_flags16(mc6809 *cpu, uint16_t v)
{
    cpu->cc &= (uint8_t)~CC_V;
    set_nz16(cpu, v);
}

/*
 * Operacoes de leitura-modificacao-escrita (NEG, COM, LSR, ...).
 * 'op' e o nibble baixo do opcode.  Retorna o resultado; *write indica se
 * o resultado deve ser gravado (TST nao grava).
 */
static uint8_t alu_rmw(mc6809 *cpu, int op, uint8_t m, int *write)
{
    unsigned r;
    *write = 1;
    switch (op) {
    case 0x0: case 0x1:                         /* NEG (0x01 nao doc.) */
    neg:
        r = (uint8_t)(0u - m);
        cpu->cc &= (uint8_t)~(CC_V | CC_C);
        if (m == 0x80) cpu->cc |= CC_V;
        if (m != 0)    cpu->cc |= CC_C;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0x2:                                   /* nao doc.: NEG/COM */
        if (!(cpu->cc & CC_C)) goto neg;
        /* fallthrough */
    case 0x3:                                   /* COM */
        r = (uint8_t)~m;
        cpu->cc &= (uint8_t)~CC_V;
        cpu->cc |= CC_C;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0x4: case 0x5:                         /* LSR (0x05 nao doc.) */
        cpu->cc &= (uint8_t)~CC_C;
        if (m & 1) cpu->cc |= CC_C;
        r = m >> 1;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0x6:                                   /* ROR */
        r = (m >> 1) | ((cpu->cc & CC_C) ? 0x80 : 0);
        cpu->cc &= (uint8_t)~CC_C;
        if (m & 1) cpu->cc |= CC_C;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0x7:                                   /* ASR */
        r = (m >> 1) | (m & 0x80);
        cpu->cc &= (uint8_t)~CC_C;
        if (m & 1) cpu->cc |= CC_C;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0x8:                                   /* ASL/LSL */
        r = (unsigned)m << 1;
        cpu->cc &= (uint8_t)~(CC_V | CC_C);
        if (m & 0x80)              cpu->cc |= CC_C;
        if ((m ^ (m << 1)) & 0x80) cpu->cc |= CC_V;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0x9:                                   /* ROL */
        r = ((unsigned)m << 1) | ((cpu->cc & CC_C) ? 1 : 0);
        cpu->cc &= (uint8_t)~(CC_V | CC_C);
        if (m & 0x80)              cpu->cc |= CC_C;
        if ((m ^ (m << 1)) & 0x80) cpu->cc |= CC_V;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0xA: case 0xB:                         /* DEC (0x0B nao doc.) */
        r = (uint8_t)(m - 1);
        cpu->cc &= (uint8_t)~CC_V;
        if (m == 0x80) cpu->cc |= CC_V;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0xC:                                   /* INC */
        r = (uint8_t)(m + 1);
        cpu->cc &= (uint8_t)~CC_V;
        if (m == 0x7f) cpu->cc |= CC_V;
        set_nz8(cpu, (uint8_t)r);
        return (uint8_t)r;
    case 0xD:                                   /* TST */
        cpu->cc &= (uint8_t)~CC_V;
        set_nz8(cpu, m);
        *write = 0;
        return m;
    case 0xF:                                   /* CLR */
    default:
        cpu->cc &= (uint8_t)~(CC_N | CC_V | CC_C);
        cpu->cc |= CC_Z;
        return 0;
    }
}

/* ------------------------------------------------------------------------ */
/* Enderecamento                                                            */
/* ------------------------------------------------------------------------ */

static uint16_t *index_reg(mc6809 *cpu, uint8_t pb)
{
    switch ((pb >> 5) & 3) {
    case 0:  return &cpu->x;
    case 1:  return &cpu->y;
    case 2:  return &cpu->u;
    default: return &cpu->s;
    }
}

static uint16_t ea_indexed(mc6809 *cpu, int *cyc)
{
    uint8_t pb = fetch8(cpu);
    uint16_t *r = index_reg(cpu, pb);
    uint16_t ea;

    if (!(pb & 0x80)) {
        /* deslocamento de 5 bits com sinal */
        int off = pb & 0x1f;
        if (off & 0x10) off -= 0x20;
        *cyc += 1;
        return (uint16_t)(*r + off);
    }

    switch (pb & 0x0f) {
    case 0x0: ea = *r; *r += 1; *cyc += 2; break;              /* ,R+   */
    case 0x1: ea = *r; *r += 2; *cyc += 3; break;              /* ,R++  */
    case 0x2: *r -= 1; ea = *r; *cyc += 2; break;              /* ,-R   */
    case 0x3: *r -= 2; ea = *r; *cyc += 3; break;              /* ,--R  */
    case 0x4: ea = *r; break;                                  /* ,R    */
    case 0x5: ea = (uint16_t)(*r + (int8_t)cpu->b); *cyc += 1; break;
    case 0x6: ea = (uint16_t)(*r + (int8_t)cpu->a); *cyc += 1; break;
    case 0x8: ea = (uint16_t)(*r + (int8_t)fetch8(cpu)); *cyc += 1; break;
    case 0x9: ea = (uint16_t)(*r + fetch16(cpu)); *cyc += 4; break;
    case 0xB: ea = (uint16_t)(*r + GET_D()); *cyc += 4; break;
    case 0xC: {
        int8_t off = (int8_t)fetch8(cpu);
        ea = (uint16_t)(cpu->pc + off);
        *cyc += 1;
        break;
    }
    case 0xD: {
        uint16_t off = fetch16(cpu);
        ea = (uint16_t)(cpu->pc + off);
        *cyc += 5;
        break;
    }
    case 0xF: ea = fetch16(cpu); *cyc += 2; break;             /* [n16] */
    default:  ea = *r; break;                                  /* ilegal */
    }

    if (pb & 0x10) {
        ea = rd16(cpu, ea);
        *cyc += 3;
    }
    return ea;
}

/* mode: 1 = direto, 2 = indexado, 3 = estendido */
static uint16_t ea_mode(mc6809 *cpu, int mode, int *cyc)
{
    switch (mode) {
    case 1:  return (uint16_t)((cpu->dp << 8) | fetch8(cpu));
    case 2:  return ea_indexed(cpu, cyc);
    default: return fetch16(cpu);
    }
}

/* ------------------------------------------------------------------------ */
/* Registradores para TFR/EXG                                               */
/* ------------------------------------------------------------------------ */

static uint16_t reg_get(mc6809 *cpu, int r)
{
    switch (r) {
    case 0x0: return GET_D();
    case 0x1: return cpu->x;
    case 0x2: return cpu->y;
    case 0x3: return cpu->u;
    case 0x4: return cpu->s;
    case 0x5: return cpu->pc;
    case 0x8: return (uint16_t)(0xff00 | cpu->a);
    case 0x9: return (uint16_t)(0xff00 | cpu->b);
    case 0xA: return (uint16_t)(0xff00 | cpu->cc);
    case 0xB: return (uint16_t)(0xff00 | cpu->dp);
    default:  return 0xffff;
    }
}

static void reg_set(mc6809 *cpu, int r, uint16_t v)
{
    switch (r) {
    case 0x0: SET_D(v); break;
    case 0x1: cpu->x = v; break;
    case 0x2: cpu->y = v; break;
    case 0x3: cpu->u = v; break;
    case 0x4: cpu->s = v; cpu->nmi_armed = 1; break;
    case 0x5: cpu->pc = v; break;
    case 0x8: cpu->a = (uint8_t)v; break;
    case 0x9: cpu->b = (uint8_t)v; break;
    case 0xA: cpu->cc = (uint8_t)v; break;
    case 0xB: cpu->dp = (uint8_t)v; break;
    default: break;
    }
}

/* ------------------------------------------------------------------------ */
/* Pilha                                                                    */
/* ------------------------------------------------------------------------ */

static int op_push(mc6809 *cpu, uint8_t pb, uint16_t *sp, uint16_t other)
{
    int cyc = 0;
    if (pb & 0x80) { push16(cpu, sp, cpu->pc); cyc += 2; }
    if (pb & 0x40) { push16(cpu, sp, other);   cyc += 2; }
    if (pb & 0x20) { push16(cpu, sp, cpu->y);  cyc += 2; }
    if (pb & 0x10) { push16(cpu, sp, cpu->x);  cyc += 2; }
    if (pb & 0x08) { push8(cpu, sp, cpu->dp);  cyc += 1; }
    if (pb & 0x04) { push8(cpu, sp, cpu->b);   cyc += 1; }
    if (pb & 0x02) { push8(cpu, sp, cpu->a);   cyc += 1; }
    if (pb & 0x01) { push8(cpu, sp, cpu->cc);  cyc += 1; }
    return cyc;
}

static int op_pull(mc6809 *cpu, uint8_t pb, uint16_t *sp, uint16_t *other)
{
    int cyc = 0;
    if (pb & 0x01) { cpu->cc = pull8(cpu, sp);  cyc += 1; }
    if (pb & 0x02) { cpu->a  = pull8(cpu, sp);  cyc += 1; }
    if (pb & 0x04) { cpu->b  = pull8(cpu, sp);  cyc += 1; }
    if (pb & 0x08) { cpu->dp = pull8(cpu, sp);  cyc += 1; }
    if (pb & 0x10) { cpu->x  = pull16(cpu, sp); cyc += 2; }
    if (pb & 0x20) { cpu->y  = pull16(cpu, sp); cyc += 2; }
    if (pb & 0x40) { *other  = pull16(cpu, sp); cyc += 2; }
    if (pb & 0x80) { cpu->pc = pull16(cpu, sp); cyc += 2; }
    return cyc;
}

static void push_entire(mc6809 *cpu)
{
    cpu->cc |= CC_E;
    op_push(cpu, 0xff, &cpu->s, cpu->u);
}

/* ------------------------------------------------------------------------ */
/* Desvios                                                                  */
/* ------------------------------------------------------------------------ */

static int branch_cond(mc6809 *cpu, int n)
{
    uint8_t cc = cpu->cc;
    int nf = (cc & CC_N) != 0, vf = (cc & CC_V) != 0;
    int zf = (cc & CC_Z) != 0, cf = (cc & CC_C) != 0;
    switch (n & 0xf) {
    case 0x0: return 1;                    /* BRA */
    case 0x1: return 0;                    /* BRN */
    case 0x2: return !(cf || zf);          /* BHI */
    case 0x3: return cf || zf;             /* BLS */
    case 0x4: return !cf;                  /* BCC/BHS */
    case 0x5: return cf;                   /* BCS/BLO */
    case 0x6: return !zf;                  /* BNE */
    case 0x7: return zf;                   /* BEQ */
    case 0x8: return !vf;                  /* BVC */
    case 0x9: return vf;                   /* BVS */
    case 0xA: return !nf;                  /* BPL */
    case 0xB: return nf;                   /* BMI */
    case 0xC: return nf == vf;             /* BGE */
    case 0xD: return nf != vf;             /* BLT */
    case 0xE: return !zf && (nf == vf);    /* BGT */
    default:  return zf || (nf != vf);     /* BLE */
    }
}

/* ------------------------------------------------------------------------ */
/* API                                                                      */
/* ------------------------------------------------------------------------ */

void mc6809_init(mc6809 *cpu, void *ctx, mc6809_read_fn rd, mc6809_write_fn wr)
{
    cpu->ctx = ctx;
    cpu->read = rd;
    cpu->write = wr;
    cpu->a = cpu->b = cpu->dp = 0;
    cpu->x = cpu->y = cpu->u = cpu->s = 0;
    cpu->cycles = 0;
    cpu->nmi_line = cpu->firq_line = cpu->irq_line = 0;
    mc6809_reset(cpu);
}

void mc6809_reset(mc6809 *cpu)
{
    cpu->dp = 0;
    cpu->cc = CC_I | CC_F;
    cpu->nmi_latched = 0;
    cpu->nmi_armed = 0;
    cpu->cwai = 0;
    cpu->sync = 0;
    cpu->pc = rd16(cpu, 0xfffe);
}

void mc6809_set_nmi(mc6809 *cpu, int level)
{
    if (level && !cpu->nmi_line)
        cpu->nmi_latched = 1;          /* borda de descida do /NMI */
    cpu->nmi_line = level ? 1 : 0;
}

void mc6809_set_firq(mc6809 *cpu, int level) { cpu->firq_line = level ? 1 : 0; }
void mc6809_set_irq(mc6809 *cpu, int level)  { cpu->irq_line  = level ? 1 : 0; }

static int take_interrupts(mc6809 *cpu)
{
    if (cpu->nmi_latched && cpu->nmi_armed) {
        int cyc = cpu->cwai ? 7 : 19;
        cpu->nmi_latched = 0;
        if (!cpu->cwai) push_entire(cpu);
        cpu->cc |= CC_I | CC_F;
        cpu->pc = rd16(cpu, 0xfffc);
        cpu->cwai = cpu->sync = 0;
        return cyc;
    }
    if (cpu->firq_line && !(cpu->cc & CC_F)) {
        int cyc = cpu->cwai ? 7 : 10;
        if (!cpu->cwai) {
            cpu->cc &= (uint8_t)~CC_E;
            push16(cpu, &cpu->s, cpu->pc);
            push8(cpu, &cpu->s, cpu->cc);
        }
        cpu->cc |= CC_I | CC_F;
        cpu->pc = rd16(cpu, 0xfff6);
        cpu->cwai = cpu->sync = 0;
        return cyc;
    }
    if (cpu->irq_line && !(cpu->cc & CC_I)) {
        int cyc = cpu->cwai ? 7 : 19;
        if (!cpu->cwai) push_entire(cpu);
        cpu->cc |= CC_I;
        cpu->pc = rd16(cpu, 0xfff8);
        cpu->cwai = cpu->sync = 0;
        return cyc;
    }
    return 0;
}

static int exec_page2(mc6809 *cpu);
static int exec_page3(mc6809 *cpu);

int mc6809_step(mc6809 *cpu)
{
    int cyc = take_interrupts(cpu);
    uint8_t op;

    if (cyc) {
        cpu->cycles += (uint64_t)cyc;
        return cyc;
    }
    if (cpu->cwai) {
        cpu->cycles += 1;
        return 1;
    }
    if (cpu->sync) {
        /* SYNC termina com qualquer interrupcao, mesmo mascarada */
        if (cpu->nmi_line || cpu->firq_line || cpu->irq_line) {
            cpu->sync = 0;
        } else {
            cpu->cycles += 1;
            return 1;
        }
    }

    op = fetch8(cpu);

    switch (op) {
    /* ---- direto: leitura-modificacao-escrita ---- */
    case 0x00: case 0x01: case 0x02: case 0x03: case 0x04: case 0x05:
    case 0x06: case 0x07: case 0x08: case 0x09: case 0x0a: case 0x0b:
    case 0x0c: case 0x0d: case 0x0f: {
        int wr;
        uint16_t ea = (uint16_t)((cpu->dp << 8) | fetch8(cpu));
        uint8_t r = alu_rmw(cpu, op & 0xf, RD(ea), &wr);
        if (wr) WR(ea, r);
        cyc = 6;
        break;
    }
    case 0x0e: cpu->pc = (uint16_t)((cpu->dp << 8) | fetch8(cpu)); cyc = 3; break;

    case 0x10: cyc = exec_page2(cpu); break;
    case 0x11: cyc = exec_page3(cpu); break;
    case 0x12: cyc = 2; break;                                   /* NOP */
    case 0x13: cpu->sync = 1; cyc = 4; break;                    /* SYNC */
    case 0x16: {                                                 /* LBRA */
        uint16_t off = fetch16(cpu);
        cpu->pc = (uint16_t)(cpu->pc + off);
        cyc = 5;
        break;
    }
    case 0x17: {                                                 /* LBSR */
        uint16_t off = fetch16(cpu);
        push16(cpu, &cpu->s, cpu->pc);
        cpu->pc = (uint16_t)(cpu->pc + off);
        cyc = 9;
        break;
    }
    case 0x19: {                                                 /* DAA */
        uint8_t a = cpu->a, cf = 0;
        uint8_t msn = a & 0xf0, lsn = a & 0x0f;
        unsigned r;
        if (lsn > 0x09 || (cpu->cc & CC_H)) cf |= 0x06;
        if (msn > 0x80 && lsn > 0x09)       cf |= 0x60;
        if (msn > 0x90 || (cpu->cc & CC_C)) cf |= 0x60;
        r = (unsigned)a + cf;
        cpu->cc &= (uint8_t)~CC_V;
        if (r & 0x100) cpu->cc |= CC_C;
        cpu->a = (uint8_t)r;
        set_nz8(cpu, cpu->a);
        cyc = 2;
        break;
    }
    case 0x1a: cpu->cc |= fetch8(cpu); cyc = 3; break;           /* ORCC */
    case 0x1c: cpu->cc &= fetch8(cpu); cyc = 3; break;           /* ANDCC */
    case 0x1d:                                                   /* SEX */
        cpu->a = (cpu->b & 0x80) ? 0xff : 0x00;
        set_nz16(cpu, GET_D());
        cyc = 2;
        break;
    case 0x1e: {                                                 /* EXG */
        uint8_t pb = fetch8(cpu);
        uint16_t t1 = reg_get(cpu, pb >> 4);
        uint16_t t2 = reg_get(cpu, pb & 0xf);
        reg_set(cpu, pb >> 4, t2);
        reg_set(cpu, pb & 0xf, t1);
        cyc = 8;
        break;
    }
    case 0x1f: {                                                 /* TFR */
        uint8_t pb = fetch8(cpu);
        reg_set(cpu, pb & 0xf, reg_get(cpu, pb >> 4));
        cyc = 6;
        break;
    }

    /* ---- desvios curtos ---- */
    case 0x20: case 0x21: case 0x22: case 0x23: case 0x24: case 0x25:
    case 0x26: case 0x27: case 0x28: case 0x29: case 0x2a: case 0x2b:
    case 0x2c: case 0x2d: case 0x2e: case 0x2f: {
        int8_t off = (int8_t)fetch8(cpu);
        if (branch_cond(cpu, op)) cpu->pc = (uint16_t)(cpu->pc + off);
        cyc = 3;
        break;
    }

    case 0x30:                                                   /* LEAX */
        cyc = 4; cpu->x = ea_indexed(cpu, &cyc);
        cpu->cc &= (uint8_t)~CC_Z; if (!cpu->x) cpu->cc |= CC_Z;
        break;
    case 0x31:                                                   /* LEAY */
        cyc = 4; cpu->y = ea_indexed(cpu, &cyc);
        cpu->cc &= (uint8_t)~CC_Z; if (!cpu->y) cpu->cc |= CC_Z;
        break;
    case 0x32: cyc = 4; cpu->s = ea_indexed(cpu, &cyc); cpu->nmi_armed = 1; break;
    case 0x33: cyc = 4; cpu->u = ea_indexed(cpu, &cyc); break;
    case 0x34: { uint8_t pb = fetch8(cpu); cyc = 5 + op_push(cpu, pb, &cpu->s, cpu->u); break; }
    case 0x35: { uint8_t pb = fetch8(cpu); cyc = 5 + op_pull(cpu, pb, &cpu->s, &cpu->u); break; }
    case 0x36: { uint8_t pb = fetch8(cpu); cyc = 5 + op_push(cpu, pb, &cpu->u, cpu->s); break; }
    case 0x37: { uint8_t pb = fetch8(cpu); cyc = 5 + op_pull(cpu, pb, &cpu->u, &cpu->s); break; }
    case 0x39: cpu->pc = pull16(cpu, &cpu->s); cyc = 5; break;   /* RTS */
    case 0x3a: cpu->x = (uint16_t)(cpu->x + cpu->b); cyc = 3; break; /* ABX */
    case 0x3b:                                                   /* RTI */
        cpu->cc = pull8(cpu, &cpu->s);
        if (cpu->cc & CC_E) {
            op_pull(cpu, 0xfe, &cpu->s, &cpu->u);
            cyc = 15;
        } else {
            cpu->pc = pull16(cpu, &cpu->s);
            cyc = 6;
        }
        break;
    case 0x3c:                                                   /* CWAI */
        cpu->cc &= fetch8(cpu);
        push_entire(cpu);
        cpu->cwai = 1;
        cyc = 20;
        break;
    case 0x3d: {                                                 /* MUL */
        uint16_t r = (uint16_t)(cpu->a * cpu->b);
        SET_D(r);
        cpu->cc &= (uint8_t)~(CC_Z | CC_C);
        if (r == 0)    cpu->cc |= CC_Z;
        if (r & 0x80)  cpu->cc |= CC_C;
        cyc = 11;
        break;
    }
    case 0x3f:                                                   /* SWI */
        push_entire(cpu);
        cpu->cc |= CC_I | CC_F;
        cpu->pc = rd16(cpu, 0xfffa);
        cyc = 19;
        break;

    /* ---- inerentes A / B ---- */
    case 0x40: case 0x41: case 0x42: case 0x43: case 0x44: case 0x45:
    case 0x46: case 0x47: case 0x48: case 0x49: case 0x4a: case 0x4b:
    case 0x4c: case 0x4d: case 0x4e: case 0x4f: {
        int wr;
        uint8_t r = alu_rmw(cpu, op & 0xf, cpu->a, &wr);
        if (wr) cpu->a = r;
        cyc = 2;
        break;
    }
    case 0x50: case 0x51: case 0x52: case 0x53: case 0x54: case 0x55:
    case 0x56: case 0x57: case 0x58: case 0x59: case 0x5a: case 0x5b:
    case 0x5c: case 0x5d: case 0x5e: case 0x5f: {
        int wr;
        uint8_t r = alu_rmw(cpu, op & 0xf, cpu->b, &wr);
        if (wr) cpu->b = r;
        cyc = 2;
        break;
    }

    /* ---- indexado / estendido: leitura-modificacao-escrita ---- */
    case 0x6e: cyc = 3; cpu->pc = ea_indexed(cpu, &cyc); break; /* JMP */
    case 0x7e: cpu->pc = fetch16(cpu); cyc = 4; break;          /* JMP */
    case 0x60: case 0x61: case 0x62: case 0x63: case 0x64: case 0x65:
    case 0x66: case 0x67: case 0x68: case 0x69: case 0x6a: case 0x6b:
    case 0x6c: case 0x6d: case 0x6f:
    case 0x70: case 0x71: case 0x72: case 0x73: case 0x74: case 0x75:
    case 0x76: case 0x77: case 0x78: case 0x79: case 0x7a: case 0x7b:
    case 0x7c: case 0x7d: case 0x7f: {
        int wr;
        uint16_t ea;
        uint8_t r;
        if (op & 0x10) { ea = fetch16(cpu); cyc = 7; }
        else           { cyc = 6; ea = ea_indexed(cpu, &cyc); }
        r = alu_rmw(cpu, op & 0xf, RD(ea), &wr);
        if (wr) WR(ea, r);
        break;
    }

    /* ---- 0x80..0xFF: operacoes com acumuladores ---- */
    default:
        if (op >= 0x80) {
            int isb  = (op & 0x40) != 0;
            int mode = (op >> 4) & 3;     /* 0 imed, 1 dir, 2 idx, 3 ext */
            int lo   = op & 0x0f;
            uint8_t *acc = isb ? &cpu->b : &cpu->a;

            switch (lo) {
            case 0x3: case 0xc: case 0xe: {
                /* operandos de 16 bits */
                uint16_t m;
                int is_load = (lo == 0xe) || (lo == 0xc && isb);
                if (mode == 0) {
                    m = fetch16(cpu);
                    cyc = is_load ? 3 : 4;
                } else {
                    uint16_t ea;
                    cyc = is_load ? 5 : 6;
                    if (mode == 3) cyc++;
                    ea = ea_mode(cpu, mode, &cyc);
                    m = rd16(cpu, ea);
                }
                if (lo == 0x3) {
                    if (isb) SET_D(alu_add16(cpu, GET_D(), m));  /* ADDD */
                    else     SET_D(alu_sub16(cpu, GET_D(), m));  /* SUBD */
                } else if (lo == 0xc) {
                    if (isb) { SET_D(m); ld_flags16(cpu, m); }   /* LDD */
                    else     alu_sub16(cpu, cpu->x, m);          /* CMPX */
                } else {
                    if (isb) { cpu->u = m; ld_flags16(cpu, m); } /* LDU */
                    else     { cpu->x = m; ld_flags16(cpu, m); } /* LDX */
                }
                break;
            }
            case 0x7: {                                     /* STA/STB */
                uint16_t ea;
                if (mode == 0) { cyc = 2; break; }          /* ilegal */
                cyc = (mode == 3) ? 5 : 4;
                ea = ea_mode(cpu, mode, &cyc);
                WR(ea, *acc);
                ld_flags8(cpu, *acc);
                break;
            }
            case 0xd: {
                uint16_t ea;
                if (!isb) {
                    if (mode == 0) {                        /* BSR */
                        int8_t off = (int8_t)fetch8(cpu);
                        push16(cpu, &cpu->s, cpu->pc);
                        cpu->pc = (uint16_t)(cpu->pc + off);
                        cyc = 7;
                    } else {                                /* JSR */
                        cyc = (mode == 3) ? 8 : 7;
                        ea = ea_mode(cpu, mode, &cyc);
                        push16(cpu, &cpu->s, cpu->pc);
                        cpu->pc = ea;
                    }
                } else {                                    /* STD */
                    if (mode == 0) { cyc = 2; break; }
                    cyc = (mode == 3) ? 6 : 5;
                    ea = ea_mode(cpu, mode, &cyc);
                    wr16(cpu, ea, GET_D());
                    ld_flags16(cpu, GET_D());
                }
                break;
            }
            case 0xf: {                                     /* STX/STU */
                uint16_t ea, v;
                if (mode == 0) { cyc = 2; break; }
                cyc = (mode == 3) ? 6 : 5;
                ea = ea_mode(cpu, mode, &cyc);
                /* lido depois do calculo do endereco: STX ,X++ grava o X
                 * ja incrementado, como no 6809 real */
                v = isb ? cpu->u : cpu->x;
                wr16(cpu, ea, v);
                ld_flags16(cpu, v);
                break;
            }
            default: {
                /* operandos de 8 bits */
                uint8_t m;
                if (mode == 0) {
                    m = fetch8(cpu);
                    cyc = 2;
                } else {
                    uint16_t ea;
                    cyc = (mode == 3) ? 5 : 4;
                    ea = ea_mode(cpu, mode, &cyc);
                    m = RD(ea);
                }
                switch (lo) {
                case 0x0: *acc = alu_sub8(cpu, *acc, m, 0); break;          /* SUB */
                case 0x1: alu_sub8(cpu, *acc, m, 0); break;                 /* CMP */
                case 0x2: *acc = alu_sub8(cpu, *acc, m, cpu->cc & CC_C); break; /* SBC */
                case 0x4: *acc &= m; ld_flags8(cpu, *acc); break;           /* AND */
                case 0x5: ld_flags8(cpu, (uint8_t)(*acc & m)); break;       /* BIT */
                case 0x6: *acc = m; ld_flags8(cpu, m); break;               /* LD  */
                case 0x8: *acc ^= m; ld_flags8(cpu, *acc); break;           /* EOR */
                case 0x9: *acc = alu_add8(cpu, *acc, m, cpu->cc & CC_C); break; /* ADC */
                case 0xa: *acc |= m; ld_flags8(cpu, *acc); break;           /* OR  */
                case 0xb: *acc = alu_add8(cpu, *acc, m, 0); break;          /* ADD */
                default: break;
                }
                break;
            }
            }
        } else {
            cyc = 2;   /* opcode ilegal: trata como NOP */
        }
        break;
    }

    cpu->cycles += (uint64_t)cyc;
    return cyc;
}

/* ------------------------------------------------------------------------ */
/* Pagina 2 (prefixo $10)                                                   */
/* ------------------------------------------------------------------------ */

static int exec_page2(mc6809 *cpu)
{
    uint8_t op = fetch8(cpu);
    int cyc;

    if (op >= 0x21 && op <= 0x2f) {                 /* desvios longos */
        uint16_t off = fetch16(cpu);
        cyc = 5;
        if (branch_cond(cpu, op)) {
            cpu->pc = (uint16_t)(cpu->pc + off);
            cyc = 6;
        }
        return cyc;
    }

    switch (op) {
    case 0x3f:                                      /* SWI2 */
        push_entire(cpu);
        cpu->pc = rd16(cpu, 0xfff4);
        return 20;
    case 0x83: case 0x93: case 0xa3: case 0xb3:     /* CMPD */
    case 0x8c: case 0x9c: case 0xac: case 0xbc: {   /* CMPY */
        int mode = (op >> 4) & 3;
        uint16_t m;
        if (mode == 0) { m = fetch16(cpu); cyc = 5; }
        else {
            uint16_t ea;
            cyc = (mode == 3) ? 8 : 7;
            ea = ea_mode(cpu, mode, &cyc);
            m = rd16(cpu, ea);
        }
        alu_sub16(cpu, (op & 0x0f) == 0x3 ? GET_D() : cpu->y, m);
        return cyc;
    }
    case 0x8e: case 0x9e: case 0xae: case 0xbe:     /* LDY */
    case 0xce: case 0xde: case 0xee: case 0xfe: {   /* LDS */
        int mode = (op >> 4) & 3;
        uint16_t m;
        if (mode == 0) { m = fetch16(cpu); cyc = 4; }
        else {
            uint16_t ea;
            cyc = (mode == 3) ? 7 : 6;
            ea = ea_mode(cpu, mode, &cyc);
            m = rd16(cpu, ea);
        }
        if (op & 0x40) { cpu->s = m; cpu->nmi_armed = 1; }
        else           cpu->y = m;
        ld_flags16(cpu, m);
        return cyc;
    }
    case 0x9f: case 0xaf: case 0xbf:                /* STY */
    case 0xdf: case 0xef: case 0xff: {              /* STS */
        int mode = (op >> 4) & 3;
        uint16_t ea, v;
        cyc = (mode == 3) ? 7 : 6;
        ea = ea_mode(cpu, mode, &cyc);
        v = (op & 0x40) ? cpu->s : cpu->y;   /* depois do endereco (ver STX) */
        wr16(cpu, ea, v);
        ld_flags16(cpu, v);
        return cyc;
    }
    default:
        return 2;   /* ilegal */
    }
}

/* ------------------------------------------------------------------------ */
/* Pagina 3 (prefixo $11)                                                   */
/* ------------------------------------------------------------------------ */

static int exec_page3(mc6809 *cpu)
{
    uint8_t op = fetch8(cpu);
    int cyc;

    switch (op) {
    case 0x3f:                                      /* SWI3 */
        push_entire(cpu);
        cpu->pc = rd16(cpu, 0xfff2);
        return 20;
    case 0x83: case 0x93: case 0xa3: case 0xb3:     /* CMPU */
    case 0x8c: case 0x9c: case 0xac: case 0xbc: {   /* CMPS */
        int mode = (op >> 4) & 3;
        uint16_t m;
        if (mode == 0) { m = fetch16(cpu); cyc = 5; }
        else {
            uint16_t ea;
            cyc = (mode == 3) ? 8 : 7;
            ea = ea_mode(cpu, mode, &cyc);
            m = rd16(cpu, ea);
        }
        alu_sub16(cpu, (op & 0x0f) == 0x3 ? cpu->u : cpu->s, m);
        return cyc;
    }
    default:
        return 2;   /* ilegal */
    }
}
