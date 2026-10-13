/*
 * main.c - Frontend "headless" (sem interface): executa a maquina por N
 * quadros, opcionalmente digitando texto, e salva a tela final em PPM.
 * Util para testes automatizados e para demonstrar que o nucleo nao
 * depende de nenhuma biblioteca grafica.
 *
 * Uso: coco-headless [--rom-dir DIR] [--frames N] [--type TEXTO]
 *                    [--ppm saida.ppm] [-0 disco.dsk] [--cas fita.cas]
 *                    [--rom arquivo] (ROM de 8K em $A000, ignora --rom-dir)
 *                    [--wav saida.raw] (audio PCM 16 bits mono 44100 Hz)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coco.h"
#include "media.h"

typedef struct headless {
    uint8_t last[COCO_SCREEN_W * COCO_SCREEN_H];
    FILE   *audio_out;
} headless;

static void video_frame(void *user, const uint8_t *pixels, int w, int h)
{
    headless *hl = (headless *)((media *)user)->app;
    memcpy(hl->last, pixels, (size_t)w * (size_t)h);
}

static void audio_samples(void *user, const int16_t *samples, int count)
{
    headless *hl = (headless *)((media *)user)->app;
    if (hl->audio_out)
        fwrite(samples, sizeof(int16_t), (size_t)count, hl->audio_out);
}

static int save_ppm(const char *path, const uint8_t *fb)
{
    FILE *f = fopen(path, "wb");
    int i;
    if (!f)
        return -1;
    fprintf(f, "P6\n%d %d\n255\n", COCO_SCREEN_W, COCO_SCREEN_H);
    for (i = 0; i < COCO_SCREEN_W * COCO_SCREEN_H; i++) {
        uint32_t c = coco_palette_rgb(fb[i]);
        fputc((c >> 16) & 0xff, f);
        fputc((c >> 8) & 0xff, f);
        fputc(c & 0xff, f);
    }
    fclose(f);
    return 0;
}

/* Converte um caractere ASCII em tecla do CoCo (+ shift). */
static int ascii_to_key(char ch, int *shift)
{
    *shift = 0;
    if (ch >= 'A' && ch <= 'Z') return COCO_KEY_A + (ch - 'A');
    if (ch >= 'a' && ch <= 'z') return COCO_KEY_A + (ch - 'a');
    if (ch >= '0' && ch <= '9') return COCO_KEY_0 + (ch - '0');
    switch (ch) {
    case ' ':  return COCO_KEY_SPACE;
    case '\n': return COCO_KEY_ENTER;
    case '@':  return COCO_KEY_AT;
    case ':':  return COCO_KEY_COLON;
    case ';':  return COCO_KEY_SEMICOLON;
    case ',':  return COCO_KEY_COMMA;
    case '-':  return COCO_KEY_MINUS;
    case '.':  return COCO_KEY_PERIOD;
    case '/':  return COCO_KEY_SLASH;
    case '!':  *shift = 1; return COCO_KEY_1;
    case '"':  *shift = 1; return COCO_KEY_2;
    case '#':  *shift = 1; return COCO_KEY_3;
    case '$':  *shift = 1; return COCO_KEY_4;
    case '%':  *shift = 1; return COCO_KEY_5;
    case '&':  *shift = 1; return COCO_KEY_6;
    case '\'': *shift = 1; return COCO_KEY_7;
    case '(':  *shift = 1; return COCO_KEY_8;
    case ')':  *shift = 1; return COCO_KEY_9;
    case '*':  *shift = 1; return COCO_KEY_COLON;
    case '+':  *shift = 1; return COCO_KEY_SEMICOLON;
    case '<':  *shift = 1; return COCO_KEY_COMMA;
    case '=':  *shift = 1; return COCO_KEY_MINUS;
    case '>':  *shift = 1; return COCO_KEY_PERIOD;
    case '?':  *shift = 1; return COCO_KEY_SLASH;
    default:   return -1;
    }
}

int main(int argc, char **argv)
{
    headless hl;
    media md;
    coco_host host;
    coco_config cfg;
    coco *m;
    const char *rom_dir = "roms", *rom = NULL, *ppm = NULL, *type = NULL;
    const char *cas = NULL, *wav = NULL;
    const char *disks[MEDIA_MAX_DRIVES] = { NULL, NULL, NULL, NULL };
    int frames = 300, i, boot_frames = 120;
    coco_cpu_state st;

    memset(&hl, 0, sizeof(hl));
    memset(&md, 0, sizeof(md));
    memset(&host, 0, sizeof(host));
    memset(&cfg, 0, sizeof(cfg));
    cfg.model = COCO_MODEL_2;
    cfg.audio_rate = 44100;

    for (i = 1; i < argc; i++) {
        const char *o = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
        if (!v && strcmp(o, "--help")) { fprintf(stderr, "faltou argumento para %s\n", o); return 1; }
        if (!strcmp(o, "--rom-dir"))      { rom_dir = v; i++; }
        else if (!strcmp(o, "--rom"))     { rom = v; i++; }
        else if (!strcmp(o, "--frames"))  { frames = atoi(v); i++; }
        else if (!strcmp(o, "--boot"))    { boot_frames = atoi(v); i++; }
        else if (!strcmp(o, "--type"))    { type = v; i++; }
        else if (!strcmp(o, "--ppm"))     { ppm = v; i++; }
        else if (!strcmp(o, "--cas"))     { cas = v; i++; }
        else if (!strcmp(o, "--wav"))     { wav = v; i++; }
        else if (!strcmp(o, "--model"))   { cfg.model = atoi(v) == 1 ? COCO_MODEL_1 : COCO_MODEL_2; i++; }
        else if (!strcmp(o, "--ram"))     { cfg.ram_kb = atoi(v); i++; }
        else if (o[0] == '-' && o[1] >= '0' && o[1] <= '3' && !o[2]) { disks[o[1] - '0'] = v; i++; }
        else { fprintf(stderr, "opcao desconhecida: %s\n", o); return 1; }
    }

    md.app = &hl;
    media_attach(&md, &host);
    host.video_frame = video_frame;
    host.audio_samples = audio_samples;
    if (wav) hl.audio_out = fopen(wav, "wb");

    m = coco_create(&cfg, &host);
    if (rom) {
        size_t size;
        uint8_t *data = media_read_file(rom, &size);
        if (!data) { fprintf(stderr, "nao foi possivel ler %s\n", rom); return 1; }
        coco_load_rom(m, COCO_ROM_COLOR_BASIC, data, size);
        free(data);
    } else if (!(media_load_roms(m, rom_dir, 1, 1) & 1)) {
        fprintf(stderr, "ROM do Color BASIC nao encontrada em %s\n", rom_dir);
        return 1;
    }
    for (i = 0; i < MEDIA_MAX_DRIVES; i++)
        if (disks[i]) media_disk_insert(&md, i, disks[i], 0);
    if (cas) media_tape_insert(&md, cas);
    coco_reset(m, 1);

    for (i = 0; i < boot_frames && i < frames; i++)
        coco_run_frame(m);

    /* digita o texto: cada tecla fica 3 quadros pressionada e 3 solta */
    if (type) {
        const char *p;
        for (p = type; *p; p++) {
            int shift, key, k;
            char ch = *p;
            if (ch == '\\' && p[1] == 'n') { ch = '\n'; p++; }
            key = ascii_to_key(ch, &shift);
            if (key < 0) continue;
            coco_key_set(m, COCO_KEY_SHIFT, shift);
            coco_key_set(m, key, 1);
            for (k = 0; k < 3; k++) coco_run_frame(m);
            coco_key_set(m, key, 0);
            coco_key_set(m, COCO_KEY_SHIFT, 0);
            for (k = 0; k < 3; k++) coco_run_frame(m);
        }
    }

    for (; i < frames; i++)
        coco_run_frame(m);

    coco_get_cpu_state(m, &st);
    printf("PC=%04X A=%02X B=%02X X=%04X Y=%04X U=%04X S=%04X DP=%02X CC=%02X ciclos=%llu\n",
           st.pc, st.a, st.b, st.x, st.y, st.u, st.s, st.dp, st.cc,
           (unsigned long long)st.cycles);

    if (ppm && save_ppm(ppm, hl.last) != 0)
        fprintf(stderr, "falha ao gravar %s\n", ppm);

    if (hl.audio_out) fclose(hl.audio_out);
    coco_destroy(m);
    media_free(&md);
    return 0;
}
