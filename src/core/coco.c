/*
 * coco.c - Maquina TRS-80 Color Computer 1/2: barramento, mapa de memoria,
 * temporizacao de video, audio e integracao dos perifericos.
 *
 * Mapa de memoria (SAM no modo de mapa 0):
 *   $0000-$7FFF  RAM (P1 seleciona os 32K superiores em maquinas de 64K)
 *   $8000-$9FFF  Extended Color BASIC
 *   $A000-$BFFF  Color BASIC
 *   $C000-$FEFF  Cartucho (ex.: Disk BASIC)
 *   $FF00-$FF1F  PIA0 (teclado, joystick, HS/FS)
 *   $FF20-$FF3F  PIA1 (DAC, cassete, som, modo do VDG)
 *   $FF40-$FF5F  SCS do cartucho (controlador de disco)
 *   $FFC0-$FFDF  SAM
 *   $FFE0-$FFFF  vetores (lidos de $BFE0-$BFFF)
 * No modo de mapa 1 (TY=1) toda a faixa $0000-$FEFF e RAM.
 *
 * Temporizacao: o tempo e contado em "ticks" de 1,789772 MHz (o dobro do
 * clock normal da CPU de 0,894886 MHz), de modo que o modo de velocidade
 * dupla do SAM (R1=1) e tratado naturalmente.  Um quadro NTSC tem 262
 * linhas de 114 ticks (57 ciclos de CPU).
 */
#include <stdlib.h>
#include <string.h>

#include "coco.h"
#include "mc6809.h"
#include "mc6821.h"
#include "sam.h"
#include "vdg.h"
#include "wd1793.h"
#include "cassette.h"

#define TICK_HZ          1789772
#define TICKS_PER_LINE   114
#define LINES_PER_FRAME  262
#define FIRST_VISIBLE    14      /* primeira linha que vai para o framebuffer */
#define ACTIVE_START     38      /* primeira linha da area ativa */
#define ACTIVE_LINES     192
#define HS_LOW_TICKS     10
#define AUDIO_BUF_MAX    4096

struct coco {
    coco_config cfg;
    coco_host   host;

    mc6809   cpu;
    mc6821   pia0, pia1;
    sam      sam;
    wd1793   fdc;
    cassette cas;

    uint8_t  ram[0x10000];
    uint32_t ram_mask;
    uint8_t  rom_basic[0x2000];
    uint8_t  rom_ext[0x2000];
    uint8_t  rom_cart[0x4000];
    size_t   cart_size;
    int      has_ext;
    int      has_basic;

    int      disk_enabled;
    int      cart_autostart;
    uint8_t  ff40;
    int      halt_enable;

    /* entrada */
    uint8_t  matrix[8];       /* linhas pressionadas por coluna */
    int      joy_axis[2][2];  /* [joystick][0=x,1=y] 0..63 */
    int      joy_button[2];

    /* video */
    uint8_t  fb[COCO_SCREEN_W * COCO_SCREEN_H];
    uint8_t  vdg_mode;

    /* temporizacao */
    int      tick_budget;
    int      cart_q;

    /* audio */
    int16_t  abuf[AUDIO_BUF_MAX];
    int      alen;
    int64_t  a_sum;
    int      a_cnt;
    int64_t  a_phase;
    int32_t  hp_prev_in;
    int32_t  hp_prev_out;
};

/* ------------------------------------------------------------------------ */
/* Paleta                                                                   */
/* ------------------------------------------------------------------------ */

static const uint32_t default_palette[COCO_NUM_COLORS] = {
    0x0aff0a,   /* verde */
    0xffff43,   /* amarelo */
    0x2a17ff,   /* azul */
    0xbc0522,   /* vermelho */
    0xffffff,   /* "buff" (branco amarelado) */
    0x0ad46f,   /* ciano */
    0xff1cff,   /* magenta */
    0xff4a08,   /* laranja */
    0x090909,   /* preto */
    0x003f00,   /* verde escuro (fundo do texto) */
    0x6a1600,   /* laranja escuro */
    0x1a62ff,   /* artefato azul */
    0xff5a1a,   /* artefato vermelho/laranja */
    0xffffff    /* branco */
};

uint32_t coco_palette_rgb(int color)
{
    if (color < 0 || color >= COCO_NUM_COLORS)
        return 0;
    return default_palette[color];
}

/* ------------------------------------------------------------------------ */
/* Sinais internos                                                          */
/* ------------------------------------------------------------------------ */

static void update_cpu_irqs(coco *m)
{
    mc6809_set_irq(&m->cpu, mc6821_irq_a(&m->pia0) || mc6821_irq_b(&m->pia0));
    mc6809_set_firq(&m->cpu, mc6821_irq_a(&m->pia1) || mc6821_irq_b(&m->pia1));
}

static void update_nmi(coco *m)
{
    mc6809_set_nmi(&m->cpu, m->disk_enabled && m->fdc.intrq && (m->ff40 & 0x20));
}

static int dac_value(coco *m)
{
    return mc6821_port_value(&m->pia1.a) >> 2;
}

static int sound_mux(coco *m)
{
    int sel1 = mc6821_c2(&m->pia0.a);
    int sel2 = mc6821_c2(&m->pia0.b);
    return (sel2 << 1) | sel1;
}

static void update_audio_level_dependencies(coco *m)
{
    /* saida do DAC tambem alimenta a gravacao em fita */
    cassette_output(&m->cas, dac_value(m));
}

/* ------------------------------------------------------------------------ */
/* Callbacks das PIAs                                                       */
/* ------------------------------------------------------------------------ */

static void pia_irq_changed(void *ctx, mc6821 *pia)
{
    (void)pia;
    update_cpu_irqs((coco *)ctx);
}

static void pia0_pre_read(void *ctx, mc6821 *pia, int port)
{
    coco *m = (coco *)ctx;
    if (port == 0) {
        uint8_t cols = mc6821_port_value(&pia->b);
        uint8_t rows = 0x7f;
        int col, sel, js;
        for (col = 0; col < 8; col++)
            if (!(cols & (1 << col)))
                rows &= (uint8_t)~m->matrix[col];
        /* botoes: direito em PA0, esquerdo em PA1 */
        if (m->joy_button[0]) rows &= (uint8_t)~0x01;
        if (m->joy_button[1]) rows &= (uint8_t)~0x02;
        /* comparador do joystick em PA7 */
        sel = sound_mux(m);
        js = m->joy_axis[sel >> 1][sel & 1];
        if (js >= dac_value(m))
            rows |= 0x80;
        pia->a.in = rows;
    } else {
        pia->b.in = 0xff;
    }
}

static void pia0_output_changed(void *ctx, mc6821 *pia, int port)
{
    (void)ctx; (void)pia; (void)port;
}

static void pia1_pre_read(void *ctx, mc6821 *pia, int port)
{
    coco *m = (coco *)ctx;
    if (port == 0) {
        /* PA0 = entrada do cassete */
        pia->a.in = (uint8_t)(0xfe | cassette_input(&m->cas));
    } else {
        /* PB2 = jumper de tamanho da RAM (1 = chips de 16K/64K, 0 = 4K);
         * o Color BASIC usa este bit para programar o SAM.
         * PB0 = entrada RS-232 (repouso em 1) */
        uint8_t v = 0xff;
        if (m->cfg.ram_kb <= 4) v &= (uint8_t)~0x04;
        pia->b.in = v;
    }
}

static void pia1_output_changed(void *ctx, mc6821 *pia, int port)
{
    coco *m = (coco *)ctx;
    if (port == 0) {
        /* CA2 = motor do cassete; PA2-PA7 = DAC */
        cassette_set_motor(&m->cas, mc6821_c2(&pia->a));
        update_audio_level_dependencies(m);
    } else {
        /* PB3-PB7 = modo do VDG */
        m->vdg_mode = (uint8_t)(mc6821_port_value(&pia->b) & 0xf8);
    }
}

/* ------------------------------------------------------------------------ */
/* Controlador de disco                                                     */
/* ------------------------------------------------------------------------ */

static void fdc_intrq_changed(void *ctx, int level)
{
    coco *m = (coco *)ctx;
    if (level)
        m->halt_enable = 0;
    update_nmi(m);
}

static void fdc_drq_changed(void *ctx, int level)
{
    (void)ctx; (void)level;
}

static void ff40_write(coco *m, uint8_t v)
{
    int drive = -1;
    m->ff40 = v;
    if (v & 0x01)      drive = 0;
    else if (v & 0x02) drive = 1;
    else if (v & 0x04) drive = 2;
    /* b3 motor, b4 pre-compensacao, b5 densidade dupla + NMI,
     * b6 seleciona face (DS3), b7 habilita HALT */
    wd1793_select(&m->fdc, drive, (v & 0x40) ? 1 : 0, (v & 0x20) ? 1 : 0);
    m->halt_enable = (v & 0x80) && !m->fdc.intrq;
    update_nmi(m);
}

/* ------------------------------------------------------------------------ */
/* Barramento                                                               */
/* ------------------------------------------------------------------------ */

static uint32_t ram_index(coco *m, uint16_t addr)
{
    uint32_t a = addr;
    /* P1 so troca os 32K inferiores pelos superiores no modo de 64K do
     * SAM (M=2) com mapa 0.  O Color BASIC deixa P1=1 no modo de 16K,
     * onde o bit nao tem efeito. */
    if (a < 0x8000 && sam_page1(&m->sam) && !sam_map_type(&m->sam) &&
        sam_mem_size(&m->sam) == 2)
        a += 0x8000;
    return a & m->ram_mask;
}

static uint8_t rom_read(coco *m, uint16_t addr)
{
    if (addr < 0xa000)
        return m->has_ext ? m->rom_ext[addr - 0x8000] : 0xff;
    if (addr < 0xc000)
        return m->rom_basic[addr - 0xa000];
    if (m->cart_size)
        return m->rom_cart[(addr - 0xc000) % m->cart_size];
    return 0xff;
}

static uint8_t io_read(coco *m, uint16_t addr)
{
    if (addr < 0xff20)
        return mc6821_read(&m->pia0, addr & 3);
    if (addr < 0xff40)
        return mc6821_read(&m->pia1, addr & 3);
    if (addr < 0xff60) {
        if (m->disk_enabled && addr >= 0xff48 && addr < 0xff50)
            return wd1793_read(&m->fdc, addr & 3);
        return 0xff;
    }
    if (addr < 0xffe0)
        return 0xff;
    return m->rom_basic[addr - 0xe000];          /* $FFE0 -> $BFE0 */
}

static void io_write(coco *m, uint16_t addr, uint8_t val)
{
    if (addr < 0xff20) {
        mc6821_write(&m->pia0, addr & 3, val);
    } else if (addr < 0xff40) {
        mc6821_write(&m->pia1, addr & 3, val);
    } else if (addr < 0xff60) {
        if (m->disk_enabled) {
            if (addr < 0xff48)
                ff40_write(m, val);
            else if (addr < 0xff50)
                wd1793_write(&m->fdc, addr & 3, val);
        }
    } else if (addr >= 0xffc0 && addr < 0xffe0) {
        sam_write(&m->sam, addr);
    }
}

static uint8_t bus_read(void *ctx, uint16_t addr)
{
    coco *m = (coco *)ctx;
    if (addr < 0x8000)
        return m->ram[ram_index(m, addr)];
    if (addr >= 0xff00)
        return io_read(m, addr);
    if (sam_map_type(&m->sam))
        return m->ram[addr & m->ram_mask];
    return rom_read(m, addr);
}

static void bus_write(void *ctx, uint16_t addr, uint8_t val)
{
    coco *m = (coco *)ctx;
    if (addr < 0x8000) {
        m->ram[ram_index(m, addr)] = val;
    } else if (addr >= 0xff00) {
        io_write(m, addr, val);
    } else {
        /* escrita na faixa de ROM vai para a RAM por baixo (64K) */
        m->ram[addr & m->ram_mask] = val;
    }
}

/* ------------------------------------------------------------------------ */
/* Audio                                                                    */
/* ------------------------------------------------------------------------ */

static int audio_level(coco *m)
{
    int level = 0;
    /* CB2 da PIA1 habilita o multiplexador de som */
    if (mc6821_c2(&m->pia1.b)) {
        switch (sound_mux(m)) {
        case 0: level = (dac_value(m) - 32) * 600; break;
        case 1: level = cassette_input(&m->cas) ? 6000 : -6000; break;
        default: break;
        }
    }
    /* som de 1 bit em PB1 */
    if ((m->pia1.b.ddr & 0x02) && (m->pia1.b.out & 0x02))
        level += 8000;
    return level;
}

static void audio_emit(coco *m, int32_t in)
{
    /* filtro passa-altas simples para remover nivel DC */
    int32_t out = in - m->hp_prev_in + (m->hp_prev_out * 4063) / 4096;
    m->hp_prev_in = in;
    m->hp_prev_out = out;
    if (out > 32767)  out = 32767;
    if (out < -32768) out = -32768;
    if (m->alen < AUDIO_BUF_MAX)
        m->abuf[m->alen++] = (int16_t)out;
}

static void audio_advance(coco *m, int ticks)
{
    int level;
    if (m->cfg.audio_rate <= 0)
        return;
    level = audio_level(m);
    m->a_sum += (int64_t)level * ticks;
    m->a_cnt += ticks;
    m->a_phase += (int64_t)ticks * m->cfg.audio_rate;
    while (m->a_phase >= TICK_HZ) {
        m->a_phase -= TICK_HZ;
        audio_emit(m, m->a_cnt ? (int32_t)(m->a_sum / m->a_cnt) : level);
        m->a_sum = 0;
        m->a_cnt = 0;
    }
}

static void audio_flush(coco *m)
{
    if (m->alen && m->host.audio_samples)
        m->host.audio_samples(m->host.user, m->abuf, m->alen);
    m->alen = 0;
}

/* ------------------------------------------------------------------------ */
/* Execucao                                                                 */
/* ------------------------------------------------------------------------ */

static void run_ticks(coco *m, int ticks)
{
    m->tick_budget += ticks;
    while (m->tick_budget > 0) {
        int t;
        if (m->disk_enabled && m->halt_enable && !m->fdc.drq) {
            t = 2;                               /* CPU parada por /HALT */
        } else {
            int cyc = mc6809_step(&m->cpu);
            t = (sam_mpu_rate(&m->sam) & 2) ? cyc : cyc * 2;
        }
        m->tick_budget -= t;
        cassette_tick(&m->cas, t);
        if (m->disk_enabled)
            wd1793_tick(&m->fdc, t);
        audio_advance(m, t);
    }
}

static const int sam_ydiv[8]   = { 12, 3, 3, 2, 2, 1, 1, 1 };
static const int sam_xbytes[8] = { 32, 16, 32, 16, 32, 16, 32, 32 };

static void render_line(coco *m, int line)
{
    int fy = line - FIRST_VISIBLE;
    uint8_t *dst;
    uint8_t border;
    int x;

    if (fy < 0 || fy >= COCO_SCREEN_H)
        return;
    dst = &m->fb[fy * COCO_SCREEN_W];
    border = vdg_border_color(m->vdg_mode);

    if (line >= ACTIVE_START && line < ACTIVE_START + ACTIVE_LINES) {
        int y = line - ACTIVE_START;
        int smode = sam_vdg_mode(&m->sam);
        uint16_t addr = (uint16_t)(sam_vdg_base(&m->sam) +
                                   (y / sam_ydiv[smode]) * sam_xbytes[smode]);
        uint8_t data[32];
        int n = vdg_bytes_per_line(m->vdg_mode), i;
        for (i = 0; i < n; i++)
            data[i] = m->ram[(uint16_t)(addr + i) & m->ram_mask];
        for (x = 0; x < COCO_ACTIVE_X; x++) dst[x] = border;
        vdg_render_line(dst + COCO_ACTIVE_X, data, m->vdg_mode, y % 12, m->cfg.artifact);
        for (x = COCO_ACTIVE_X + 256; x < COCO_SCREEN_W; x++) dst[x] = border;
    } else {
        memset(dst, border, COCO_SCREEN_W);
    }
}

void coco_run_frame(coco *m)
{
    int line;
    for (line = 0; line < LINES_PER_FRAME; line++) {
        /* FS (sincronismo de quadro) em PIA0 CB1: cai ao fim da area ativa */
        if (line == ACTIVE_START + ACTIVE_LINES)
            mc6821_set_c1(&m->pia0, 1, 0);
        else if (line == 0)
            mc6821_set_c1(&m->pia0, 1, 1);

        render_line(m, line);

        /* HS (sincronismo horizontal) em PIA0 CA1 */
        mc6821_set_c1(&m->pia0, 0, 0);
        run_ticks(m, HS_LOW_TICKS);
        mc6821_set_c1(&m->pia0, 0, 1);

        /* sinal CART (autostart) em PIA1 CB1 */
        if (m->cart_size && m->cart_autostart) {
            m->cart_q ^= 1;
            mc6821_set_c1(&m->pia1, 1, m->cart_q);
        }
        run_ticks(m, TICKS_PER_LINE - HS_LOW_TICKS);
    }
    if (m->host.video_frame)
        m->host.video_frame(m->host.user, m->fb, COCO_SCREEN_W, COCO_SCREEN_H);
    audio_flush(m);
}

/* ------------------------------------------------------------------------ */
/* Ciclo de vida                                                            */
/* ------------------------------------------------------------------------ */

coco *coco_create(const coco_config *cfg, const coco_host *host)
{
    coco *m = (coco *)calloc(1, sizeof(*m));
    int kb;
    if (!m)
        return NULL;
    if (cfg) {
        m->cfg = *cfg;
    } else {
        m->cfg.model = COCO_MODEL_2;
        m->cfg.ram_kb = 64;
        m->cfg.audio_rate = 44100;
        m->cfg.artifact = COCO_ARTIFACT_OFF;
    }
    if (m->cfg.model != COCO_MODEL_1)
        m->cfg.model = COCO_MODEL_2;
    kb = m->cfg.ram_kb;
    if (kb <= 0) kb = (m->cfg.model == COCO_MODEL_1) ? 32 : 64;
    if (kb <= 4) kb = 4;
    else if (kb <= 16) kb = 16;
    else if (kb <= 32) kb = 32;
    else kb = 64;
    m->cfg.ram_kb = kb;
    m->ram_mask = (uint32_t)kb * 1024 - 1;

    if (host)
        m->host = *host;

    memset(m->rom_basic, 0xff, sizeof(m->rom_basic));
    memset(m->rom_ext, 0xff, sizeof(m->rom_ext));

    m->pia0.ctx = m;
    m->pia0.pre_read = pia0_pre_read;
    m->pia0.output_changed = pia0_output_changed;
    m->pia0.irq_changed = pia_irq_changed;
    m->pia1.ctx = m;
    m->pia1.pre_read = pia1_pre_read;
    m->pia1.output_changed = pia1_output_changed;
    m->pia1.irq_changed = pia_irq_changed;

    cassette_init(&m->cas, &m->host);
    wd1793_init(&m->fdc, &m->host, m);
    m->fdc.intrq_changed = fdc_intrq_changed;
    m->fdc.drq_changed = fdc_drq_changed;

    mc6809_init(&m->cpu, m, bus_read, bus_write);
    coco_reset(m, 1);
    return m;
}

void coco_destroy(coco *m)
{
    free(m);
}

int coco_load_rom(coco *m, coco_rom_slot slot, const uint8_t *data, size_t size)
{
    if (!data || !size)
        return -1;
    switch (slot) {
    case COCO_ROM_COLOR_BASIC:
        if (size > sizeof(m->rom_basic)) size = sizeof(m->rom_basic);
        memset(m->rom_basic, 0xff, sizeof(m->rom_basic));
        memcpy(m->rom_basic, data, size);
        m->has_basic = 1;
        return 0;
    case COCO_ROM_EXTENDED_BASIC:
        if (size > sizeof(m->rom_ext)) size = sizeof(m->rom_ext);
        memset(m->rom_ext, 0xff, sizeof(m->rom_ext));
        memcpy(m->rom_ext, data, size);
        m->has_ext = 1;
        return 0;
    case COCO_ROM_CARTRIDGE:
        if (size > sizeof(m->rom_cart)) size = sizeof(m->rom_cart);
        memcpy(m->rom_cart, data, size);
        m->cart_size = size;
        return 0;
    }
    return -1;
}

void coco_eject_cartridge(coco *m)
{
    m->cart_size = 0;
    m->cart_autostart = 0;
}

void coco_enable_disk_controller(coco *m, int enable)
{
    m->disk_enabled = enable ? 1 : 0;
    update_nmi(m);
}

void coco_set_cartridge_autostart(coco *m, int enable)
{
    m->cart_autostart = enable ? 1 : 0;
    if (!enable)
        mc6821_set_c1(&m->pia1, 1, 1);
}

void coco_reset(coco *m, int cold)
{
    if (cold) {
        /* conteudo inicial da RAM: padrao alternado, como em DRAM real */
        size_t i;
        for (i = 0; i < sizeof(m->ram); i++)
            m->ram[i] = (i & 0x80) ? 0xff : 0x00;
        m->ram[0x71] = 0;   /* garante partida a frio do BASIC */
    }
    sam_reset(&m->sam);
    mc6821_reset(&m->pia0);
    mc6821_reset(&m->pia1);
    wd1793_reset(&m->fdc);
    m->ff40 = 0;
    m->halt_enable = 0;
    m->tick_budget = 0;
    mc6809_reset(&m->cpu);
    update_cpu_irqs(m);
    update_nmi(m);
}

/* ------------------------------------------------------------------------ */
/* Entrada e utilitarios                                                    */
/* ------------------------------------------------------------------------ */

void coco_key_set(coco *m, int key, int pressed)
{
    int row, col;
    if (key < 0 || key >= COCO_KEY_COUNT)
        return;
    row = key >> 3;
    col = key & 7;
    if (row > 6)
        return;
    if (pressed) m->matrix[col] |= (uint8_t)(1 << row);
    else         m->matrix[col] &= (uint8_t)~(1 << row);
}

void coco_keys_clear(coco *m)
{
    memset(m->matrix, 0, sizeof(m->matrix));
}

void coco_joystick_set(coco *m, int joy, int x, int y, int button)
{
    if (joy < 0 || joy > 1)
        return;
    if (x < 0) x = 0;
    if (x > 63) x = 63;
    if (y < 0) y = 0;
    if (y > 63) y = 63;
    m->joy_axis[joy][0] = x;
    m->joy_axis[joy][1] = y;
    m->joy_button[joy] = button ? 1 : 0;
}

void coco_set_artifact(coco *m, coco_artifact artifact)
{
    m->cfg.artifact = artifact;
}

void coco_cassette_rewind(coco *m)
{
    cassette_rewind(&m->cas);
}

uint8_t coco_peek(coco *m, uint16_t addr)
{
    if (addr >= 0xff00 && addr < 0xffe0)
        return 0xff;
    return bus_read(m, addr);
}

void coco_poke(coco *m, uint16_t addr, uint8_t val)
{
    if (addr >= 0xff00)
        return;
    bus_write(m, addr, val);
}

void coco_get_cpu_state(coco *m, coco_cpu_state *st)
{
    st->a = m->cpu.a;
    st->b = m->cpu.b;
    st->dp = m->cpu.dp;
    st->cc = m->cpu.cc;
    st->x = m->cpu.x;
    st->y = m->cpu.y;
    st->u = m->cpu.u;
    st->s = m->cpu.s;
    st->pc = m->cpu.pc;
    st->cycles = m->cpu.cycles;
}

void coco_get_io_state(coco *m, coco_io_state *st)
{
    st->sam = m->sam.reg;
    st->vdg_mode = m->vdg_mode;
    st->pia0_cra = m->pia0.a.cr;  st->pia0_crb = m->pia0.b.cr;
    st->pia0_ddra = m->pia0.a.ddr; st->pia0_ddrb = m->pia0.b.ddr;
    st->pia0_ora = m->pia0.a.out; st->pia0_orb = m->pia0.b.out;
    st->pia1_cra = m->pia1.a.cr;  st->pia1_crb = m->pia1.b.cr;
    st->pia1_ddra = m->pia1.a.ddr; st->pia1_ddrb = m->pia1.b.ddr;
    st->pia1_ora = m->pia1.a.out; st->pia1_orb = m->pia1.b.out;
    st->ff40 = m->ff40;
    st->fdc_status = m->fdc.status;
    st->fdc_track = m->fdc.track;
    st->fdc_sector = m->fdc.sector;
    st->fdc_command = m->fdc.command;
    st->irq = m->cpu.irq_line;
    st->firq = m->cpu.firq_line;
    st->nmi = m->cpu.nmi_line;
    st->halted = m->disk_enabled && m->halt_enable && !m->fdc.drq;
}

/* ------------------------------------------------------------------------ */
/* Stub de boot (execucao sem as ROMs da Tandy)                             */
/* ------------------------------------------------------------------------ */

void coco_load_boot_stub(coco *m, uint16_t entry)
{
    /* vetores $FFF2-$FFFF, lidos de $BFF2-$BFFF */
    static const uint16_t ram_vectors[6] = {
        0x0100,     /* SWI3 */
        0x0103,     /* SWI2 */
        0x010f,     /* FIRQ */
        0x010c,     /* IRQ  */
        0x0106,     /* SWI  */
        0x0109      /* NMI  */
    };
    int i;
    memset(m->rom_basic, 0xff, sizeof(m->rom_basic));
    for (i = 0; i < 6; i++) {
        m->rom_basic[0x1ff2 + 2 * i] = (uint8_t)(ram_vectors[i] >> 8);
        m->rom_basic[0x1ff3 + 2 * i] = (uint8_t)ram_vectors[i];
    }
    m->rom_basic[0x1ffe] = (uint8_t)(entry >> 8);
    m->rom_basic[0x1fff] = (uint8_t)entry;
    m->has_basic = 1;
}

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                */
/* ------------------------------------------------------------------------ */

#define SNAP_MAGIC    "COCOSNAP"
#define SNAP_VERSION  1
#define SNAP_HEADER   (8 + 2 + 1 + 2 + 4)

typedef struct snapbuf {
    uint8_t       *w;       /* escrita (NULL ao ler/medir) */
    const uint8_t *r;       /* leitura */
    size_t         pos;
} snapbuf;

/* Cada campo e serializado por uma unica funcao "sync" usada nos dois
 * sentidos, o que garante que gravacao e leitura nunca divirjam. */
static void s_bytes(snapbuf *b, void *p, size_t n)
{
    if (b->w)      memcpy(b->w + b->pos, p, n);
    else if (b->r) memcpy(p, b->r + b->pos, n);
    b->pos += n;
}

static void s_u8(snapbuf *b, uint8_t *v)
{
    s_bytes(b, v, 1);
}

static void s_u16(snapbuf *b, uint16_t *v)
{
    uint8_t t[2];
    t[0] = (uint8_t)*v; t[1] = (uint8_t)(*v >> 8);
    s_bytes(b, t, 2);
    *v = (uint16_t)(t[0] | (t[1] << 8));
}

static void s_u32(snapbuf *b, uint32_t *v)
{
    uint8_t t[4];
    int i;
    for (i = 0; i < 4; i++) t[i] = (uint8_t)(*v >> (8 * i));
    s_bytes(b, t, 4);
    *v = 0;
    for (i = 0; i < 4; i++) *v |= (uint32_t)t[i] << (8 * i);
}

static void s_u64(snapbuf *b, uint64_t *v)
{
    uint8_t t[8];
    int i;
    for (i = 0; i < 8; i++) t[i] = (uint8_t)(*v >> (8 * i));
    s_bytes(b, t, 8);
    *v = 0;
    for (i = 0; i < 8; i++) *v |= (uint64_t)t[i] << (8 * i);
}

static void s_int(snapbuf *b, int *v)
{
    uint32_t t = (uint32_t)*v;
    s_u32(b, &t);
    *v = (int)(int32_t)t;
}

static void s_pia_port(snapbuf *b, mc6821_port *p)
{
    s_u8(b, &p->out);
    s_u8(b, &p->ddr);
    s_u8(b, &p->cr);
    s_u8(b, &p->in);
    s_int(b, &p->c1);
    s_int(b, &p->c2);
    s_int(b, &p->irq1);
    s_int(b, &p->irq2);
    s_int(b, &p->c2_out);
}

static void snapshot_sync(coco *m, snapbuf *b)
{
    mc6809 *c = &m->cpu;
    wd1793 *f = &m->fdc;
    cassette *t = &m->cas;
    int i;

    /* CPU */
    s_u8(b, &c->a);  s_u8(b, &c->b);  s_u8(b, &c->dp); s_u8(b, &c->cc);
    s_u16(b, &c->x); s_u16(b, &c->y); s_u16(b, &c->u); s_u16(b, &c->s);
    s_u16(b, &c->pc);
    s_int(b, &c->nmi_line);  s_int(b, &c->nmi_latched); s_int(b, &c->nmi_armed);
    s_int(b, &c->firq_line); s_int(b, &c->irq_line);
    s_int(b, &c->cwai);      s_int(b, &c->sync);
    s_u64(b, &c->cycles);

    /* PIAs, SAM, VDG */
    s_pia_port(b, &m->pia0.a); s_pia_port(b, &m->pia0.b);
    s_pia_port(b, &m->pia1.a); s_pia_port(b, &m->pia1.b);
    s_u16(b, &m->sam.reg);
    s_u8(b, &m->vdg_mode);

    /* glue */
    s_u8(b, &m->ff40);
    s_int(b, &m->halt_enable);
    s_int(b, &m->tick_budget);
    s_int(b, &m->cart_q);

    /* controlador de disco */
    s_u8(b, &f->status); s_u8(b, &f->track); s_u8(b, &f->sector);
    s_u8(b, &f->data);   s_u8(b, &f->command);
    s_int(b, &f->drive); s_int(b, &f->side); s_int(b, &f->dden);
    for (i = 0; i < WD_MAX_DRIVES; i++) s_int(b, &f->head_pos[i]);
    s_int(b, &f->step_dir);
    s_int(b, &f->intrq); s_int(b, &f->drq);
    s_int(b, &f->cmd_type); s_int(b, &f->state); s_int(b, &f->delay);
    s_int(b, &f->pos); s_int(b, &f->len);
    s_u64(b, &f->time);
    s_bytes(b, f->buf, sizeof(f->buf));

    /* fita (a posicao no arquivo pertence ao hospedeiro) */
    s_int(b, &t->motor); s_u64(b, &t->time);
    s_int(b, &t->eof); s_int(b, &t->byte); s_int(b, &t->bit_index);
    s_int(b, &t->half); s_int(b, &t->remaining); s_int(b, &t->level);
    s_int(b, &t->half_ticks);
    s_int(b, &t->out_high); s_u64(b, &t->out_rise_time);
    s_u8(b, &t->wbyte); s_int(b, &t->wbits);

    /* RAM */
    s_bytes(b, m->ram, (size_t)m->cfg.ram_kb * 1024);
}

/* FNV-1a sobre as ROMs carregadas: detecta snapshot de outra configuracao. */
static uint32_t rom_hash(coco *m)
{
    uint32_t h = 2166136261u;
    size_t i;
    for (i = 0; i < sizeof(m->rom_basic); i++) h = (h ^ m->rom_basic[i]) * 16777619u;
    if (m->has_ext)
        for (i = 0; i < sizeof(m->rom_ext); i++) h = (h ^ m->rom_ext[i]) * 16777619u;
    for (i = 0; i < m->cart_size; i++) h = (h ^ m->rom_cart[i]) * 16777619u;
    return h;
}

size_t coco_snapshot_size(coco *m)
{
    snapbuf b;
    b.w = NULL; b.r = NULL; b.pos = 0;
    snapshot_sync(m, &b);       /* so mede: nao le nem grava */
    return SNAP_HEADER + b.pos;
}

size_t coco_snapshot_save(coco *m, uint8_t *buf, size_t cap)
{
    size_t size = coco_snapshot_size(m);
    snapbuf b;
    uint16_t ver = SNAP_VERSION, ram = (uint16_t)m->cfg.ram_kb;
    uint8_t model = (uint8_t)m->cfg.model;
    uint32_t hash = rom_hash(m);

    if (!buf || cap < size)
        return 0;
    b.w = buf; b.r = NULL; b.pos = 0;
    s_bytes(&b, (void *)SNAP_MAGIC, 8);
    s_u16(&b, &ver);
    s_u8(&b, &model);
    s_u16(&b, &ram);
    s_u32(&b, &hash);
    snapshot_sync(m, &b);
    return b.pos;
}

int coco_snapshot_load(coco *m, const uint8_t *buf, size_t size)
{
    snapbuf b;
    char magic[8];
    uint16_t ver = 0, ram = 0;
    uint8_t model = 0;
    uint32_t hash = 0;

    if (!buf || size < SNAP_HEADER)
        return COCO_SNAPSHOT_ERR_SIZE;
    b.w = NULL; b.r = buf; b.pos = 0;
    s_bytes(&b, magic, 8);
    s_u16(&b, &ver);
    s_u8(&b, &model);
    s_u16(&b, &ram);
    s_u32(&b, &hash);
    if (memcmp(magic, SNAP_MAGIC, 8) != 0 || ver != SNAP_VERSION)
        return COCO_SNAPSHOT_ERR_FORMAT;
    if (ram != m->cfg.ram_kb)
        return COCO_SNAPSHOT_ERR_RAM;
    if (hash != rom_hash(m))
        return COCO_SNAPSHOT_ERR_ROM;
    if (size < coco_snapshot_size(m))
        return COCO_SNAPSHOT_ERR_SIZE;
    (void)model;

    /* todas as verificacoes passaram: a leitura abaixo nao falha */
    snapshot_sync(m, &b);

    /* estado derivado que nao vai no arquivo */
    m->alen = 0;
    m->a_sum = 0;
    m->a_cnt = 0;
    return COCO_SNAPSHOT_OK;
}
