/*
 * wd1793.h - Controlador de disquete Western Digital WD1793 (FD-502).
 *
 * A emulacao trabalha em nivel de setor: os dados sao obtidos/gravados
 * atraves das funcoes disk_* de coco_host, entao o formato da imagem
 * (JVC/DSK, VDK, DMK...) fica a cargo do hospedeiro.
 */
#ifndef WD1793_H
#define WD1793_H

#include <stdint.h>
#include "coco.h"

#define WD_MAX_DRIVES   4
#define WD_TRACK_BYTES  6400

typedef struct wd1793 {
    /* registradores */
    uint8_t status;
    uint8_t track;
    uint8_t sector;
    uint8_t data;
    uint8_t command;

    /* estado da mecanica */
    int     drive;                  /* unidade selecionada (-1 = nenhuma) */
    int     side;
    int     dden;                   /* densidade dupla */
    int     head_pos[WD_MAX_DRIVES];
    int     step_dir;               /* +1 para dentro, -1 para fora */

    /* sinais */
    int     intrq;
    int     drq;

    /* comando em andamento */
    int     cmd_type;               /* 1..4 */
    int     state;
    int     delay;                  /* ticks ate o proximo evento */
    uint8_t buf[WD_TRACK_BYTES];
    int     pos, len;
    uint64_t time;                  /* para o pulso de indice */

    const coco_host *host;
    void *ctx;
    void (*intrq_changed)(void *ctx, int level);
    void (*drq_changed)(void *ctx, int level);
} wd1793;

void    wd1793_init(wd1793 *fdc, const coco_host *host, void *ctx);
void    wd1793_reset(wd1793 *fdc);
uint8_t wd1793_read(wd1793 *fdc, int reg);
void    wd1793_write(wd1793 *fdc, int reg, uint8_t val);
/* avanca o tempo do controlador (ticks de 1/1.789772 MHz) */
void    wd1793_tick(wd1793 *fdc, int ticks);
void    wd1793_select(wd1793 *fdc, int drive, int side, int dden);

#endif /* WD1793_H */
