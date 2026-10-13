/*
 * test_machine.c - Teste de integracao da maquina com a ROM de teste
 * (gerada por tests/make_test_rom.py): verifica o que o VDG mostra.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coco.h"

static uint8_t fb[COCO_SCREEN_W * COCO_SCREEN_H];
static int failures, checks;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FALHA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static void video(void *u, const uint8_t *px, int w, int h)
{
    (void)u;
    memcpy(fb, px, (size_t)w * (size_t)h);
}

/* pixel (x, y) dentro da area ativa de 256x192 */
static uint8_t px(int x, int y)
{
    return fb[(COCO_ACTIVE_Y + y) * COCO_SCREEN_W + COCO_ACTIVE_X + x];
}

int main(int argc, char **argv)
{
    static uint8_t rom[0x2000];
    coco_config cfg = { COCO_MODEL_2, 64, 0, COCO_ARTIFACT_OFF };
    coco_host host;
    FILE *f;
    coco *m;
    int i;

    if (argc < 2 || !(f = fopen(argv[1], "rb")) || fread(rom, 1, sizeof(rom), f) != sizeof(rom)) {
        fprintf(stderr, "uso: test_machine testrom.rom\n");
        return 2;
    }
    fclose(f);

    memset(&host, 0, sizeof(host));
    host.video_frame = video;
    m = coco_create(&cfg, &host);
    coco_load_rom(m, COCO_ROM_COLOR_BASIC, rom, sizeof(rom));
    coco_reset(m, 1);
    for (i = 0; i < 10; i++)
        coco_run_frame(m);

    /* celula (0,0) = 'H' com bit de inverso: fundo verde claro.
     * (com a RAM inicial apareceria '@' normal, fundo verde escuro) */
    CHECK(px(0, 0) == COCO_GREEN, "fundo da celula (0,0) = %d", px(0, 0));
    CHECK(px(2, 4) == COCO_DARK_GREEN, "traco do 'H' = %d", px(2, 4));
    /* linha 1: blocos SG4 $8F,$9F,... -> cores 0..7 */
    CHECK(px(1 * 8 + 2, 12 + 2) == COCO_YELLOW, "bloco amarelo = %d", px(10, 14));
    CHECK(px(7 * 8 + 2, 12 + 8) == COCO_ORANGE, "bloco laranja = %d", px(58, 20));
    /* resto da tela preenchido com espacos (fundo verde) */
    CHECK(px(128, 100) == COCO_GREEN, "espaco = %d", px(128, 100));

    coco_destroy(m);
    printf("%d verificacoes, %d falhas\n", checks, failures);
    return failures ? 1 : 0;
}
