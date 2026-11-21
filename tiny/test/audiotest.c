/*
 * audiotest.c - Compara o som do coco4k (assembly) com o do emulador em C.
 *
 * As ROMs de teste tocam ondas quadradas no DAC com lacos de espera de
 * duracoes diferentes.  A frequencia medida nos dois emuladores deve ser a
 * mesma (dentro de uma tolerancia), e com o som desligado o coco4k deve
 * gerar silencio (80h).
 *
 * Uso: audiotest rom1 [rom2 ...]   (nome terminado em "_off" = som desligado)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <stdint.h>

#include "coco.h"

#define V_APTR (-100)
#define FRAMES 120
#define LINES  262

extern void t_frame(uint8_t *mem);
extern void t_reset(uint8_t *mem);

static uint8_t abuf[128 + 0x10000 + 256 * 192];
static uint8_t *amem = abuf + 128;
static uint8_t asamp[FRAMES * LINES];
static int16_t csamp[FRAMES * 800];
static int ncsamp;

static void audio(void *u, const int16_t *s, int n)
{
    (void)u;
    if (ncsamp + n <= (int)(sizeof(csamp) / sizeof(csamp[0]))) {
        memcpy(csamp + ncsamp, s, (size_t)n * sizeof(int16_t));
        ncsamp += n;
    }
}

int main(int argc, char **argv)
{
    int r, fails = 0;
    for (r = 1; r < argc; r++) {
        static uint8_t rom[0x2000];
        coco_config cfg = { COCO_MODEL_2, 64, 44100, COCO_ARTIFACT_OFF };
        coco_host host;
        coco *m;
        FILE *f = fopen(argv[r], "rb");
        int i, rises_a = 0, rises_c = 0, off = strstr(argv[r], "_off") != NULL;
        uint32_t ptr;
        double fa, fc;

        if (!f) { perror(argv[r]); return 2; }
        if (fread(rom, 1, sizeof(rom), f) != sizeof(rom)) { fclose(f); return 2; }
        fclose(f);

        memset(&host, 0, sizeof(host));
        host.audio_samples = audio;
        ncsamp = 0;
        m = coco_create(&cfg, &host);
        coco_load_rom(m, COCO_ROM_COLOR_BASIC, rom, sizeof(rom));
        coco_reset(m, 1);

        memset(abuf, 0, sizeof(abuf));
        memcpy(amem + 0xA000, rom, sizeof(rom));
        memcpy(amem + 0xFFF0, amem + 0xBFF0, 16);
        t_reset(amem);

        for (i = 0; i < FRAMES; i++) {
            ptr = (uint32_t)(uintptr_t)(asamp + i * LINES);
            memcpy(amem + V_APTR, &ptr, 4);
            t_frame(amem);
            coco_run_frame(m);
        }
        /* ignora o primeiro decimo (inicializacao) */
        for (i = FRAMES * LINES / 10; i < FRAMES * LINES; i++)
            if (asamp[i - 1] < 0x80 && asamp[i] >= 0x80) rises_a++;
        for (i = ncsamp / 10; i < ncsamp; i++)
            if (csamp[i - 1] < 0 && csamp[i] >= 0) rises_c++;
        fa = rises_a / (FRAMES * 0.9 / 59.923);
        fc = rises_c / (ncsamp * 0.9 / 44100.0);
        if (off) {
            int silent = 1;
            for (i = FRAMES * LINES / 10; i < FRAMES * LINES; i++)
                if (asamp[i] != 0x80) silent = 0;
            printf("%s: som desligado -> %s\n", argv[r], silent ? "silencio" : "FALHA: nao silenciou");
            fails += !silent;
        } else {
            double err = fabs(fa - fc) / fc * 100;
            printf("%s: coco4k %.1f Hz, emulador C %.1f Hz (diferenca %.1f%%)\n", argv[r], fa, fc, err);
            fails += err > 3.0;
        }
        coco_destroy(m);
    }
    return fails ? 1 : 0;
}
