/*
 * test_game.c - Testa o jogo-exemplo (examples/game/game.asm) rodando sem
 * ROMs, via coco_load_boot_stub(), e a API de snapshots.
 *
 * Uso: test_game game.bin
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "coco.h"

/* enderecos das variaveis do jogo (ver game.asm) */
#define FRAMES 0x0300
#define SCORE  0x0301
#define PPOS   0x0304
#define TPOS   0x0306

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

static uint16_t peek16(coco *m, uint16_t a)
{
    return (uint16_t)((coco_peek(m, a) << 8) | coco_peek(m, (uint16_t)(a + 1)));
}

static coco *make_machine(const uint8_t *game, size_t size, int ram_kb)
{
    coco_config cfg = { COCO_MODEL_2, 64, 0, COCO_ARTIFACT_OFF };
    coco_host host;
    coco *m;
    cfg.ram_kb = ram_kb;
    memset(&host, 0, sizeof(host));
    host.video_frame = video;
    m = coco_create(&cfg, &host);
    coco_load_boot_stub(m, 0xc000);
    coco_load_rom(m, COCO_ROM_CARTRIDGE, game, size);
    coco_reset(m, 1);
    return m;
}

static void run(coco *m, int frames)
{
    while (frames-- > 0)
        coco_run_frame(m);
}

/* Aperta as setas em direcao ao alvo ate marcar 'goal' pontos. */
static int play_until(coco *m, int goal, int max_frames)
{
    int f;
    for (f = 0; f < max_frames; f++) {
        uint16_t p = peek16(m, PPOS), t = peek16(m, TPOS);
        int pr = (p - 0x0400) / 32, pc = (p - 0x0400) % 32;
        int tr = (t - 0x0400) / 32, tc = (t - 0x0400) % 32;
        coco_keys_clear(m);
        if (tr < pr)      coco_key_set(m, COCO_KEY_UP, 1);
        else if (tr > pr) coco_key_set(m, COCO_KEY_DOWN, 1);
        else if (tc < pc) coco_key_set(m, COCO_KEY_LEFT, 1);
        else if (tc > pc) coco_key_set(m, COCO_KEY_RIGHT, 1);
        coco_run_frame(m);
        if (coco_peek(m, SCORE) >= goal) {
            coco_keys_clear(m);
            return f;
        }
    }
    coco_keys_clear(m);
    return -1;
}

int main(int argc, char **argv)
{
    static uint8_t game[0x4000];
    size_t size;
    FILE *f;
    coco *m, *other;
    uint8_t *snap, *snap2;
    size_t snap_size, n;
    uint16_t p0;
    uint8_t ram_a[0x8000], fb_a[sizeof(fb)];
    int i;

    if (argc < 2 || !(f = fopen(argv[1], "rb"))) {
        fprintf(stderr, "uso: test_game game.bin\n");
        return 2;
    }
    size = fread(game, 1, sizeof(game), f);
    fclose(f);

    /* ---- boot sem ROM ---- */
    m = make_machine(game, size, 64);
    run(m, 10);
    p0 = peek16(m, PPOS);
    CHECK(p0 == 0x0400 + 8 * 32 + 16, "posicao inicial = %04X", p0);
    CHECK(coco_peek(m, p0) == 0xcf, "jogador na tela = %02X", coco_peek(m, p0));
    CHECK(coco_peek(m, 0x0400) == 0x50, "placar 'P' = %02X", coco_peek(m, 0x0400));
    CHECK(coco_peek(m, FRAMES) >= 8, "IRQ de quadro contando: %d", coco_peek(m, FRAMES));
    CHECK(coco_peek(m, peek16(m, TPOS)) == 0xbf, "alvo desenhado");

    /* ---- teclado: seta para a direita ---- */
    coco_key_set(m, COCO_KEY_RIGHT, 1);
    run(m, 12);
    coco_key_set(m, COCO_KEY_RIGHT, 0);
    run(m, 4);
    CHECK(peek16(m, PPOS) > p0 && peek16(m, PPOS) < p0 + 8, "moveu para a direita: %04X", peek16(m, PPOS));

    /* ---- jogando: coleta um alvo ---- */
    i = play_until(m, 1, 2000);
    CHECK(i >= 0, "nao conseguiu coletar o alvo");
    run(m, 10);
    CHECK(coco_peek(m, 0x0409) == 0x71, "placar mostra 1: %02X", coco_peek(m, 0x0409));

    /* ---- snapshot: salvar, jogar, restaurar, jogar igual ---- */
    snap_size = coco_snapshot_size(m);
    snap = malloc(snap_size);
    snap2 = malloc(snap_size);
    CHECK(snap_size > 64 * 1024, "tamanho do snapshot: %u", (unsigned)snap_size);
    CHECK(coco_snapshot_save(m, snap, snap_size - 1) == 0, "buffer pequeno deve falhar");
    n = coco_snapshot_save(m, snap, snap_size);
    CHECK(n == snap_size, "bytes salvos: %u", (unsigned)n);

    play_until(m, 3, 3000);
    for (i = 0; i < 0x8000; i++) ram_a[i] = coco_peek(m, (uint16_t)i);
    memcpy(fb_a, fb, sizeof(fb));
    CHECK(coco_peek(m, SCORE) == 3, "placar 3 antes de restaurar: %d", coco_peek(m, SCORE));

    CHECK(coco_snapshot_load(m, snap, n) == COCO_SNAPSHOT_OK, "restaurar snapshot");
    CHECK(coco_peek(m, SCORE) == 1, "placar voltou para 1: %d", coco_peek(m, SCORE));
    play_until(m, 3, 3000);
    {
        int same = 1;
        for (i = 0; i < 0x8000; i++)
            if (coco_peek(m, (uint16_t)i) != ram_a[i]) { same = 0; break; }
        CHECK(same, "RAM diverge apos restaurar e repetir as entradas (end. %04X)", i);
        CHECK(memcmp(fb, fb_a, sizeof(fb)) == 0, "tela diverge apos restaurar");
    }

    /* salvar de novo deve produzir os mesmos bytes que o estado restaurado */
    CHECK(coco_snapshot_load(m, snap, n) == COCO_SNAPSHOT_OK, "restaurar de novo");
    CHECK(coco_snapshot_save(m, snap2, snap_size) == n && memcmp(snap, snap2, n) == 0,
          "salvar apos restaurar reproduz o snapshot");

    /* ---- erros ---- */
    CHECK(coco_snapshot_load(m, snap, 10) == COCO_SNAPSHOT_ERR_SIZE, "truncado no cabecalho");
    CHECK(coco_snapshot_load(m, snap, n - 1) == COCO_SNAPSHOT_ERR_SIZE, "truncado");
    snap2[0] = 'X';
    CHECK(coco_snapshot_load(m, snap2, n) == COCO_SNAPSHOT_ERR_FORMAT, "assinatura invalida");
    other = make_machine(game, size, 32);
    CHECK(coco_snapshot_load(other, snap, n) == COCO_SNAPSHOT_ERR_RAM, "RAM diferente");
    coco_destroy(other);
    game[0x100] ^= 0xff;
    other = make_machine(game, size, 64);
    CHECK(coco_snapshot_load(other, snap, n) == COCO_SNAPSHOT_ERR_ROM, "ROM diferente");
    coco_destroy(other);

    free(snap);
    free(snap2);
    coco_destroy(m);
    printf("%d verificacoes, %d falhas\n", checks, failures);
    return failures ? 1 : 0;
}
