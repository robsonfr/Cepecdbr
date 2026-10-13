/*
 * cassette.h - Interface de fita cassete do CoCo.
 *
 * Leitura: os bytes do arquivo .CAS (vindos do hospedeiro) sao convertidos
 * em onda quadrada: bit 1 = um ciclo de 2400 Hz, bit 0 = um ciclo de
 * 1200 Hz, bits enviados do menos para o mais significativo.
 * Gravacao: a saida do DAC e decodificada medindo o semiciclo positivo.
 */
#ifndef CASSETTE_H
#define CASSETTE_H

#include <stdint.h>
#include "coco.h"

typedef struct cassette {
    const coco_host *host;
    int      motor;
    uint64_t time;

    /* reproducao */
    int      eof;
    int      byte;
    int      bit_index;     /* 0..7; 8 = precisa ler novo byte */
    int      half;          /* 0 = semiciclo alto, 1 = baixo */
    int      remaining;     /* ticks restantes no semiciclo atual */
    int      level;         /* saida do comparador */
    int      half_ticks;    /* duracao do semiciclo atual */

    /* gravacao */
    int      out_high;
    uint64_t out_rise_time;
    uint8_t  wbyte;
    int      wbits;
} cassette;

void cassette_init(cassette *c, const coco_host *host);
void cassette_rewind(cassette *c);
void cassette_set_motor(cassette *c, int on);
void cassette_tick(cassette *c, int ticks);
int  cassette_input(const cassette *c);
/* nova amostra do DAC (0..63) enquanto o motor esta ligado */
void cassette_output(cassette *c, int dac);

#endif /* CASSETTE_H */
