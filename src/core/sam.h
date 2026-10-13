/*
 * sam.h - Synchronous Address Multiplexer Motorola MC6883 (SN74LS783).
 *
 * Registrador de 16 bits programado por pares de enderecos em $FFC0-$FFDF:
 * escrever no endereco par zera o bit, no impar seta o bit.
 */
#ifndef SAM_H
#define SAM_H

#include <stdint.h>

typedef struct sam {
    uint16_t reg;
} sam;

void     sam_reset(sam *s);
void     sam_write(sam *s, uint16_t addr);     /* addr em $FFC0-$FFDF */

static inline int      sam_vdg_mode(const sam *s)    { return s->reg & 0x07; }          /* V0-V2 */
static inline uint16_t sam_vdg_base(const sam *s)    { return (uint16_t)(((s->reg >> 3) & 0x7f) << 9); } /* F0-F6 */
static inline int      sam_page1(const sam *s)       { return (s->reg >> 10) & 1; }     /* P1 */
static inline int      sam_mpu_rate(const sam *s)    { return (s->reg >> 11) & 3; }     /* R0-R1 */
static inline int      sam_mem_size(const sam *s)    { return (s->reg >> 13) & 3; }     /* M0-M1 */
static inline int      sam_map_type(const sam *s)    { return (s->reg >> 15) & 1; }     /* TY */

#endif /* SAM_H */
