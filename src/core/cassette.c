/*
 * cassette.c - Interface de fita cassete do CoCo.
 */
#include "cassette.h"

#define TICKS_PER_SEC 1789772
#define HALF_1  (TICKS_PER_SEC / 2400 / 2)   /* semiciclo do bit 1 */
#define HALF_0  (TICKS_PER_SEC / 1200 / 2)   /* semiciclo do bit 0 */
#define WRITE_THRESHOLD ((HALF_0 + HALF_1) / 2)

void cassette_init(cassette *c, const coco_host *host)
{
    c->host = host;
    c->motor = 0;
    c->time = 0;
    c->out_high = 0;
    c->out_rise_time = 0;
    cassette_rewind(c);
}

void cassette_rewind(cassette *c)
{
    c->eof = 0;
    c->bit_index = 8;
    c->half = 1;
    c->remaining = 0;
    c->level = 0;
    c->half_ticks = HALF_0;
    c->wbyte = 0;
    c->wbits = 0;
}

void cassette_set_motor(cassette *c, int on)
{
    on = on ? 1 : 0;
    if (on == c->motor)
        return;
    c->motor = on;
    if (!on) {
        /* descarta bits parciais da gravacao */
        c->wbits = 0;
        c->wbyte = 0;
    }
    if (c->host && c->host->cassette_motor)
        c->host->cassette_motor(c->host->user, on);
}

static int next_bit(cassette *c)
{
    if (c->bit_index >= 8) {
        int b = -1;
        if (!c->eof && c->host && c->host->cassette_read_byte)
            b = c->host->cassette_read_byte(c->host->user);
        if (b < 0) {
            c->eof = 1;
            return -1;
        }
        c->byte = b;
        c->bit_index = 0;
    }
    return (c->byte >> c->bit_index++) & 1;
}

void cassette_tick(cassette *c, int ticks)
{
    c->time += (uint64_t)ticks;
    if (!c->motor || c->eof)
        return;
    c->remaining -= ticks;
    while (c->remaining <= 0 && !c->eof) {
        if (c->half == 0) {
            c->half = 1;
            c->level = 0;
            c->remaining += c->half_ticks;
        } else {
            int bit = next_bit(c);
            if (bit < 0) {
                c->level = 0;
                break;
            }
            c->half = 0;
            c->level = 1;
            c->half_ticks = bit ? HALF_1 : HALF_0;
            c->remaining += c->half_ticks;
        }
    }
}

int cassette_input(const cassette *c)
{
    return c->motor ? c->level : 0;
}

void cassette_output(cassette *c, int dac)
{
    int high = dac >= 32;
    if (!c->motor || high == c->out_high)
        return;
    c->out_high = high;
    if (high) {
        c->out_rise_time = c->time;
        return;
    }
    /* borda de descida: o semiciclo positivo determina o bit */
    {
        uint64_t dt = c->time - c->out_rise_time;
        int bit;
        if (dt > (uint64_t)HALF_0 * 3)
            return;   /* silencio / ruido */
        bit = dt < WRITE_THRESHOLD;
        c->wbyte = (uint8_t)((c->wbyte >> 1) | (bit ? 0x80 : 0));
        if (++c->wbits == 8) {
            if (c->host && c->host->cassette_write_byte)
                c->host->cassette_write_byte(c->host->user, c->wbyte);
            c->wbits = 0;
            c->wbyte = 0;
        }
    }
}
