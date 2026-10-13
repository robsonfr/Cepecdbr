/*
 * coco.h - API publica do nucleo do emulador TRS-80 Color Computer 1/2.
 *
 * O nucleo e 100% C99 e nao depende de nenhuma biblioteca de midia ou de
 * sistema de arquivos. Toda comunicacao com o mundo externo (video, audio,
 * disco, fita cassete) acontece pela estrutura coco_host, preenchida pelo
 * frontend (SDL, headless, libretro, etc).
 */
#ifndef COCO_H
#define COCO_H

#include <stdint.h>
#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ------------------------------------------------------------------------ */
/* Geometria do framebuffer gerado pelo nucleo                              */
/* ------------------------------------------------------------------------ */

/* Area ativa do MC6847 e 256x192; somamos uma borda visivel em volta. */
#define COCO_SCREEN_W   320
#define COCO_SCREEN_H   240
#define COCO_ACTIVE_X   32
#define COCO_ACTIVE_Y   24

/* Indices de cor gravados no framebuffer (8 bits por pixel). O frontend
 * converte para RGB usando coco_palette_rgb() ou uma paleta propria. */
enum coco_color {
    COCO_GREEN = 0,
    COCO_YELLOW,
    COCO_BLUE,
    COCO_RED,
    COCO_BUFF,
    COCO_CYAN,
    COCO_MAGENTA,
    COCO_ORANGE,
    COCO_BLACK,
    COCO_DARK_GREEN,
    COCO_DARK_ORANGE,
    COCO_ARTIFACT_BLUE,
    COCO_ARTIFACT_RED,
    COCO_WHITE,
    COCO_NUM_COLORS
};

/* ------------------------------------------------------------------------ */
/* Configuracao                                                             */
/* ------------------------------------------------------------------------ */

typedef enum {
    COCO_MODEL_1 = 1,   /* Color Computer 1 */
    COCO_MODEL_2 = 2    /* Color Computer 2 */
} coco_model;

typedef enum {
    COCO_ARTIFACT_OFF = 0,
    COCO_ARTIFACT_BLUE_RED, /* fase "azul primeiro" */
    COCO_ARTIFACT_RED_BLUE  /* fase "vermelho primeiro" */
} coco_artifact;

typedef struct coco_config {
    coco_model    model;
    int           ram_kb;       /* 4, 16, 32 ou 64 */
    int           audio_rate;   /* Hz; 0 desliga a geracao de audio */
    coco_artifact artifact;     /* cores de artefato NTSC em PMODE 4 */
} coco_config;

/* ------------------------------------------------------------------------ */
/* Interface com o hospedeiro (o nucleo e agnostico de plataforma)          */
/* ------------------------------------------------------------------------ */

/* Bits retornados por disk_status() */
#define COCO_DISK_PRESENT        0x01
#define COCO_DISK_WRITE_PROTECT  0x02
#define COCO_DISK_DOUBLE_SIDED   0x04

typedef struct coco_host {
    void *user;

    /* Video: chamado ao fim de cada quadro (~60 Hz). 'pixels' contem
     * indices enum coco_color, COCO_SCREEN_W x COCO_SCREEN_H, pitch = w. */
    void (*video_frame)(void *user, const uint8_t *pixels, int w, int h);

    /* Audio: amostras mono de 16 bits com sinal, na taxa audio_rate. */
    void (*audio_samples)(void *user, const int16_t *samples, int count);

    /* Fita cassete (leitura de arquivo .CAS): retorna o proximo byte da
     * fita (0..255) ou -1 se nao ha fita / fim de fita. */
    int  (*cassette_read_byte)(void *user);
    /* Fita cassete (gravacao): byte decodificado do sinal de saida. */
    void (*cassette_write_byte)(void *user, uint8_t byte);
    /* Motor do gravador ligado/desligado (opcional). */
    void (*cassette_motor)(void *user, int on);

    /* Disco: unidades 0..3. Setores sao enderecados fisicamente
     * (trilha, face, numero do setor como gravado no disco, base 1).
     * Retornam 0 em sucesso, <0 se o setor nao existe. */
    unsigned (*disk_status)(void *user, int drive);
    int  (*disk_read_sector)(void *user, int drive, int track, int side,
                             int sector, uint8_t *buf, int size);
    int  (*disk_write_sector)(void *user, int drive, int track, int side,
                              int sector, const uint8_t *buf, int size);
} coco_host;

/* ------------------------------------------------------------------------ */
/* Teclado                                                                  */
/* ------------------------------------------------------------------------ */

/* Codigo de tecla = (linha << 3) | coluna na matriz do CoCo.
 * Colunas sao PB0..PB7 da PIA0, linhas sao PA0..PA6. */
#define COCO_KEY(row, col) (((row) << 3) | (col))

enum coco_key {
    COCO_KEY_AT = COCO_KEY(0,0), COCO_KEY_A, COCO_KEY_B, COCO_KEY_C,
    COCO_KEY_D, COCO_KEY_E, COCO_KEY_F, COCO_KEY_G,
    COCO_KEY_H = COCO_KEY(1,0), COCO_KEY_I, COCO_KEY_J, COCO_KEY_K,
    COCO_KEY_L, COCO_KEY_M, COCO_KEY_N, COCO_KEY_O,
    COCO_KEY_P = COCO_KEY(2,0), COCO_KEY_Q, COCO_KEY_R, COCO_KEY_S,
    COCO_KEY_T, COCO_KEY_U, COCO_KEY_V, COCO_KEY_W,
    COCO_KEY_X = COCO_KEY(3,0), COCO_KEY_Y, COCO_KEY_Z, COCO_KEY_UP,
    COCO_KEY_DOWN, COCO_KEY_LEFT, COCO_KEY_RIGHT, COCO_KEY_SPACE,
    COCO_KEY_0 = COCO_KEY(4,0), COCO_KEY_1, COCO_KEY_2, COCO_KEY_3,
    COCO_KEY_4, COCO_KEY_5, COCO_KEY_6, COCO_KEY_7,
    COCO_KEY_8 = COCO_KEY(5,0), COCO_KEY_9, COCO_KEY_COLON, COCO_KEY_SEMICOLON,
    COCO_KEY_COMMA, COCO_KEY_MINUS, COCO_KEY_PERIOD, COCO_KEY_SLASH,
    COCO_KEY_ENTER = COCO_KEY(6,0), COCO_KEY_CLEAR, COCO_KEY_BREAK,
    COCO_KEY_SHIFT = COCO_KEY(6,7),
    COCO_KEY_COUNT = 64
};

/* ------------------------------------------------------------------------ */
/* ROMs                                                                     */
/* ------------------------------------------------------------------------ */

typedef enum {
    COCO_ROM_COLOR_BASIC = 0,   /* 8K em $A000 */
    COCO_ROM_EXTENDED_BASIC,    /* 8K em $8000 */
    COCO_ROM_CARTRIDGE          /* ate 16K em $C000 (ex.: Disk BASIC) */
} coco_rom_slot;

/* ------------------------------------------------------------------------ */
/* Ciclo de vida                                                            */
/* ------------------------------------------------------------------------ */

typedef struct coco coco;

coco *coco_create(const coco_config *cfg, const coco_host *host);
void  coco_destroy(coco *m);

/* Copia a imagem de ROM para o slot. Retorna 0 em sucesso. */
int   coco_load_rom(coco *m, coco_rom_slot slot, const uint8_t *data, size_t size);
/* Remove o cartucho atual. */
void  coco_eject_cartridge(coco *m);
/* Habilita o controlador de disco (FD-502: WD1793 em $FF40-$FF5F).
 * A ROM do Disk BASIC deve ser carregada em COCO_ROM_CARTRIDGE. */
void  coco_enable_disk_controller(coco *m, int enable);
/* Se verdadeiro, o cartucho dispara o sinal CART (autostart). */
void  coco_set_cartridge_autostart(coco *m, int enable);

/* reset "quente" (botao RESET) ou "frio" (desliga/liga, limpa a RAM).
 * Chame coco_reset(m, 1) depois de carregar as ROMs. */
void  coco_reset(coco *m, int cold);

/* Executa um quadro completo de video (262 linhas NTSC). */
void  coco_run_frame(coco *m);

/* ------------------------------------------------------------------------ */
/* Entrada                                                                  */
/* ------------------------------------------------------------------------ */

void  coco_key_set(coco *m, int key, int pressed);
void  coco_keys_clear(coco *m);
/* joy: 0 = direito, 1 = esquerdo. Eixos 0..63. */
void  coco_joystick_set(coco *m, int joy, int x, int y, int button);

/* ------------------------------------------------------------------------ */
/* Utilitarios                                                              */
/* ------------------------------------------------------------------------ */

/* Altera o modo de cores de artefato em tempo de execucao. */
void  coco_set_artifact(coco *m, coco_artifact artifact);
/* Notifica que a midia da fita foi trocada/rebobinada. */
void  coco_cassette_rewind(coco *m);
/* Paleta padrao: retorna 0xRRGGBB para cada enum coco_color. */
uint32_t coco_palette_rgb(int color);
/* Acesso de depuracao a memoria (sem efeitos colaterais de I/O). */
uint8_t  coco_peek(coco *m, uint16_t addr);
void     coco_poke(coco *m, uint16_t addr, uint8_t val);
/* Estado da CPU para depuracao. */
typedef struct coco_cpu_state {
    uint8_t  a, b, dp, cc;
    uint16_t x, y, u, s, pc;
    uint64_t cycles;
} coco_cpu_state;
void  coco_get_cpu_state(coco *m, coco_cpu_state *st);

#ifdef __cplusplus
}
#endif

#endif /* COCO_H */
