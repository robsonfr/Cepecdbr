/*
 * vdg.c - Video Display Generator Motorola MC6847.
 *
 * Modos alfanumericos/semigraficos (A/G = 0):
 *   D7 = 0             caractere interno 5x7, D6 = inverso
 *   D7 = 1, GM0 = 0    SG4: D6-D4 cor, D3-D0 blocos 2x2
 *   D7 = 1, GM0 = 1    SG6: D7-D6 cor (+CSS), D5-D0 blocos 2x3
 * Modos graficos (A/G = 1), GM2..GM0:
 *   0 CG1 64x64x4   1 RG1 128x64x2   2 CG2 128x64x4   3 RG2 128x96x2
 *   4 CG3 128x96x4  5 RG3 128x192x2  6 CG6 128x192x4  7 RG6 256x192x2
 */
#include "vdg.h"

extern const uint8_t vdg_font[64][12];

int vdg_bytes_per_line(uint8_t mode)
{
    if (!(mode & VDG_AG))
        return 32;
    switch ((mode >> 4) & 7) {
    case 0: case 1: case 3: case 5:
        return 16;
    default:
        return 32;
    }
}

uint8_t vdg_border_color(uint8_t mode)
{
    if (!(mode & VDG_AG))
        return COCO_BLACK;
    return (mode & VDG_CSS) ? COCO_BUFF : COCO_GREEN;
}

static void render_alpha(uint8_t *dst, const uint8_t *data, uint8_t mode, int row)
{
    int css = (mode & VDG_CSS) != 0;
    int ext = (mode & VDG_GM0) != 0;
    uint8_t fg_normal = css ? COCO_ORANGE : COCO_GREEN;
    uint8_t bg_normal = css ? COCO_DARK_ORANGE : COCO_DARK_GREEN;
    int i, x;

    if (row < 0) row = 0;
    if (row > 11) row = 11;

    for (i = 0; i < 32; i++) {
        uint8_t b = data[i];
        if (b & 0x80) {
            uint8_t color, left, right;
            if (!ext) {
                /* SG4 */
                color = (uint8_t)((b >> 4) & 7);
                if (row < 6) { left = b & 0x08; right = b & 0x04; }
                else         { left = b & 0x02; right = b & 0x01; }
            } else {
                /* SG6 */
                int seg = row / 4;
                color = (uint8_t)((css ? 4 : 0) | ((b >> 6) & 3));
                switch (seg) {
                case 0:  left = b & 0x20; right = b & 0x10; break;
                case 1:  left = b & 0x08; right = b & 0x04; break;
                default: left = b & 0x02; right = b & 0x01; break;
                }
            }
            for (x = 0; x < 4; x++) dst[x]     = left  ? color : COCO_BLACK;
            for (x = 0; x < 4; x++) dst[4 + x] = right ? color : COCO_BLACK;
        } else {
            uint8_t bits = vdg_font[b & 0x3f][row];
            uint8_t fg = fg_normal, bg = bg_normal;
            if (b & 0x40) { fg = bg_normal; bg = fg_normal; }
            for (x = 0; x < 8; x++)
                dst[x] = (bits & (0x80 >> x)) ? fg : bg;
        }
        dst += 8;
    }
}

static void render_graphics(uint8_t *dst, const uint8_t *data, uint8_t mode,
                            coco_artifact artifact)
{
    int gm = (mode >> 4) & 7;
    int css = (mode & VDG_CSS) != 0;
    int nbytes = vdg_bytes_per_line(mode);
    int i, p, k;

    if (gm & 1) {
        /* modos de resolucao: 2 cores */
        uint8_t on = css ? COCO_BUFF : COCO_GREEN;
        int width = (gm == 7) ? 1 : 2;

        if (gm == 7 && css && artifact != COCO_ARTIFACT_OFF) {
            /* artefato NTSC: pares de pixels viram cores */
            uint8_t c01 = (artifact == COCO_ARTIFACT_BLUE_RED) ? COCO_ARTIFACT_RED : COCO_ARTIFACT_BLUE;
            uint8_t c10 = (artifact == COCO_ARTIFACT_BLUE_RED) ? COCO_ARTIFACT_BLUE : COCO_ARTIFACT_RED;
            for (i = 0; i < nbytes; i++) {
                uint8_t b = data[i];
                for (p = 0; p < 4; p++) {
                    uint8_t c;
                    switch ((b >> (6 - 2 * p)) & 3) {
                    case 0:  c = COCO_BLACK; break;
                    case 1:  c = c01; break;
                    case 2:  c = c10; break;
                    default: c = COCO_WHITE; break;
                    }
                    *dst++ = c;
                    *dst++ = c;
                }
            }
            return;
        }

        for (i = 0; i < nbytes; i++) {
            uint8_t b = data[i];
            for (p = 0; p < 8; p++) {
                uint8_t c = (b & (0x80 >> p)) ? on : COCO_BLACK;
                for (k = 0; k < width; k++) *dst++ = c;
            }
        }
    } else {
        /* modos de cor: 4 cores, 2 bits por pixel */
        uint8_t base = css ? COCO_BUFF : COCO_GREEN;
        int width = (gm == 0) ? 4 : 2;
        for (i = 0; i < nbytes; i++) {
            uint8_t b = data[i];
            for (p = 0; p < 4; p++) {
                uint8_t c = (uint8_t)(base + ((b >> (6 - 2 * p)) & 3));
                for (k = 0; k < width; k++) *dst++ = c;
            }
        }
    }
}

void vdg_render_line(uint8_t *dst, const uint8_t *data, uint8_t mode,
                     int row, coco_artifact artifact)
{
    if (mode & VDG_AG)
        render_graphics(dst, data, mode, artifact);
    else
        render_alpha(dst, data, mode, row);
}
