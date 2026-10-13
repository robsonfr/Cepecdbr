/*
 * sam.c - Synchronous Address Multiplexer Motorola MC6883.
 */
#include "sam.h"

void sam_reset(sam *s)
{
    s->reg = 0;
}

void sam_write(sam *s, uint16_t addr)
{
    int bit = (addr >> 1) & 0x0f;
    if (addr & 1)
        s->reg |= (uint16_t)(1u << bit);
    else
        s->reg &= (uint16_t)~(1u << bit);
}
