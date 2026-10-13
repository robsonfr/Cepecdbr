/*
 * vdg.h - Video Display Generator Motorola MC6847.
 *
 * O byte de modo segue a ligacao do CoCo na PIA1 porta B:
 *   b3 = CSS   b4 = GM0 (INT/EXT)   b5 = GM1   b6 = GM2   b7 = A/G
 * No CoCo, A/S e INV do VDG estao ligados aos bits D7 e D6 do dado.
 */
#ifndef VDG_H
#define VDG_H

#include <stdint.h>
#include "coco.h"

#define VDG_CSS 0x08
#define VDG_GM0 0x10
#define VDG_GM1 0x20
#define VDG_GM2 0x40
#define VDG_AG  0x80

/* Numero de bytes que o VDG busca por linha no modo dado (16 ou 32). */
int     vdg_bytes_per_line(uint8_t mode);
/* Cor da borda no modo dado. */
uint8_t vdg_border_color(uint8_t mode);
/* Renderiza 256 pixels de uma linha ativa.
 *   data: bytes buscados da RAM (vdg_bytes_per_line bytes)
 *   row:  linha dentro da celula de caractere (0..11) */
void    vdg_render_line(uint8_t *dst, const uint8_t *data, uint8_t mode,
                        int row, coco_artifact artifact);

#endif /* VDG_H */
