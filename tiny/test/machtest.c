/*
 * machtest.c - Compara o coco4k (assembly) com o emulador em C rodando o
 * mesmo programa: a tela 256x192 deve ser identica pixel a pixel.
 *
 * Uso: machtest rom.bin [cart]   (cart: o binario vai em $C000 sem BASIC)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>

#include "coco.h"

#define KEYM   (-80)
#define FB     0x10000

extern void t_frame(uint8_t *mem);
extern void t_reset(uint8_t *mem);

static uint8_t abuf[128 + 0x10000 + 256 * 192];
static uint8_t *amem = abuf + 128;
static uint8_t cfb[COCO_SCREEN_W * COCO_SCREEN_H];

static void video(void *u, const uint8_t *px, int w, int h)
{
    (void)u;
    memcpy(cfb, px, (size_t)w * (size_t)h);
}

static void akey(int key, int down)
{
    int bit = (key & 7) * 8 + (key >> 3);
    if (down) amem[KEYM + bit / 8] |= (uint8_t)(1 << (bit & 7));
    else      amem[KEYM + bit / 8] &= (uint8_t)~(1 << (bit & 7));
}

static int compare(const char *when)
{
    int x, y, diff = 0;
    for (y = 0; y < 192; y++)
        for (x = 0; x < 256; x++) {
            uint8_t c = cfb[(COCO_ACTIVE_Y + y) * COCO_SCREEN_W + COCO_ACTIVE_X + x];
            uint8_t a = amem[FB + y * 256 + x];
            if (c != a) {
                if (diff < 5) printf("  %s: pixel (%d,%d): C=%d asm=%d\n", when, x, y, c, a);
                diff++;
            }
        }
    printf("%s: %d pixels diferentes\n", when, diff);
    return diff != 0;
}

int main(int argc, char **argv)
{
    static uint8_t rom[0x4000];
    coco_config cfg = { COCO_MODEL_2, 64, 0, COCO_ARTIFACT_OFF };
    coco_host host;
    coco *m;
    size_t size;
    FILE *f;
    int cart = argc > 2, i, fails = 0;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        fprintf(stderr, "uso: machtest rom.bin [cart]\n");
        return 2;
    }
    size = fread(rom, 1, sizeof(rom), f);
    fclose(f);

    memset(&host, 0, sizeof(host));
    host.video_frame = video;
    m = coco_create(&cfg, &host);
    if (cart) {
        static const uint8_t vec[14] = { 1,0, 1,3, 1,0x0F, 1,0x0C, 1,6, 1,9, 0xC0,0 };
        coco_load_boot_stub(m, 0xC000);
        coco_load_rom(m, COCO_ROM_CARTRIDGE, rom, size);
        memcpy(amem + 0xC000, rom, size);
        memcpy(amem + 0xFFF2, vec, 14);
    } else {
        coco_load_rom(m, COCO_ROM_COLOR_BASIC, rom, size);
        memcpy(amem + 0xA000, rom, size);
        memcpy(amem + 0xFFF0, amem + 0xBFF0, 16);
    }
    coco_reset(m, 1);
    t_reset(amem);

    for (i = 0; i < 60; i++) { coco_run_frame(m); t_frame(amem); }
    fails += compare("apos 60 quadros");

    if (cart) {
        /* segura a seta para a direita e depois para baixo */
        coco_key_set(m, COCO_KEY_RIGHT, 1); akey(COCO_KEY_RIGHT, 1);
        for (i = 0; i < 40; i++) { coco_run_frame(m); t_frame(amem); }
        coco_key_set(m, COCO_KEY_RIGHT, 0); akey(COCO_KEY_RIGHT, 0);
        coco_key_set(m, COCO_KEY_DOWN, 1); akey(COCO_KEY_DOWN, 1);
        for (i = 0; i < 30; i++) { coco_run_frame(m); t_frame(amem); }
        coco_key_set(m, COCO_KEY_DOWN, 0); akey(COCO_KEY_DOWN, 0);
        for (i = 0; i < 10; i++) { coco_run_frame(m); t_frame(amem); }
        fails += compare("apos mover o jogador");
        printf("  jogador: C=%02X%02X asm=%02X%02X\n", coco_peek(m, 0x304), coco_peek(m, 0x305),
               amem[0x304], amem[0x305]);
    }
    coco_destroy(m);
    return fails ? 1 : 0;
}
