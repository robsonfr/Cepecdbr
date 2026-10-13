/*
 * test_fdc.c - Testes do controlador WD1793 com um disco em memoria.
 */
#include <stdio.h>
#include <string.h>

#include "wd1793.h"

static uint8_t disk[35][18][256];
static int wp, failures, checks, intrq_count;

#define CHECK(cond, ...) do { \
    checks++; \
    if (!(cond)) { failures++; printf("FALHA %s:%d: ", __FILE__, __LINE__); printf(__VA_ARGS__); printf("\n"); } \
} while (0)

static unsigned st_cb(void *u, int d) { (void)u; return d == 0 ? (COCO_DISK_PRESENT | (wp ? COCO_DISK_WRITE_PROTECT : 0)) : 0; }
static int rd_cb(void *u, int d, int t, int s, int sec, uint8_t *buf, int n)
{
    (void)u;
    if (d || s || t >= 35 || sec < 1 || sec > 18) return -1;
    memcpy(buf, disk[t][sec - 1], (size_t)n);
    return 0;
}
static int wr_cb(void *u, int d, int t, int s, int sec, const uint8_t *buf, int n)
{
    (void)u;
    if (d || s || t >= 35 || sec < 1 || sec > 18) return -1;
    memcpy(disk[t][sec - 1], buf, (size_t)n);
    return 0;
}
static void intrq_cb(void *ctx, int level) { (void)ctx; if (level) intrq_count++; }

int main(void)
{
    coco_host host;
    wd1793 f;
    int i, t, s;

    for (t = 0; t < 35; t++)
        for (s = 0; s < 18; s++)
            memset(disk[t][s], t * 18 + s, 256);

    memset(&host, 0, sizeof(host));
    host.disk_status = st_cb;
    host.disk_read_sector = rd_cb;
    host.disk_write_sector = wr_cb;
    wd1793_init(&f, &host, NULL);
    f.intrq_changed = intrq_cb;
    wd1793_select(&f, 0, 0, 1);

    /* SEEK para a trilha 5 */
    wd1793_write(&f, 3, 5);
    wd1793_write(&f, 0, 0x14);
    CHECK(wd1793_read(&f, 0) & 0x01, "SEEK deveria estar ocupado");
    wd1793_tick(&f, 1000);
    CHECK(intrq_count == 1 && f.track == 5 && !(wd1793_read(&f, 0) & 0x01), "SEEK concluido");

    /* READ SECTOR 3 */
    wd1793_write(&f, 2, 3);
    wd1793_write(&f, 0, 0x80);
    CHECK(f.drq, "DRQ apos READ SECTOR");
    for (i = 0; i < 256; i++) {
        uint8_t v = wd1793_read(&f, 3);
        if (v != (uint8_t)(5 * 18 + 2)) { CHECK(0, "byte %d = %02X", i, v); break; }
    }
    CHECK(!f.drq, "DRQ deve cair apos 256 bytes");
    wd1793_tick(&f, 1000);
    CHECK(intrq_count == 2 && (wd1793_read(&f, 0) & 0x1d) == 0, "READ concluido sem erro");

    /* WRITE SECTOR 4 */
    wd1793_write(&f, 2, 4);
    wd1793_write(&f, 0, 0xa0);
    for (i = 0; i < 256; i++)
        wd1793_write(&f, 3, (uint8_t)i);
    wd1793_tick(&f, 1000);
    CHECK(intrq_count == 3 && disk[5][3][0] == 0 && disk[5][3][255] == 255, "WRITE gravou o setor");

    /* setor inexistente -> RNF */
    wd1793_write(&f, 2, 30);
    wd1793_write(&f, 0, 0x80);
    wd1793_tick(&f, 1000);
    CHECK(wd1793_read(&f, 0) & 0x10, "RNF esperado");

    /* protecao contra escrita */
    wp = 1;
    wd1793_write(&f, 2, 1);
    wd1793_write(&f, 0, 0xa0);
    wd1793_tick(&f, 1000);
    CHECK(wd1793_read(&f, 0) & 0x40, "WP esperado");

    /* RESTORE */
    wd1793_write(&f, 0, 0x00);
    wd1793_tick(&f, 1000);
    CHECK(f.track == 0 && (wd1793_read(&f, 0) & 0x04), "RESTORE -> trilha 0");

    printf("%d verificacoes, %d falhas\n", checks, failures);
    return failures ? 1 : 0;
}
