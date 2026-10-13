/*
 * wd1793.c - Controlador de disquete Western Digital WD1793.
 */
#include <string.h>
#include "wd1793.h"

/* bits de status */
#define ST_BUSY        0x01
#define ST_INDEX       0x02   /* tipo I */
#define ST_DRQ         0x02   /* tipo II/III */
#define ST_TRACK0      0x04   /* tipo I */
#define ST_LOST        0x04   /* tipo II/III */
#define ST_CRC         0x08
#define ST_SEEK_ERR    0x10   /* tipo I */
#define ST_RNF         0x10   /* tipo II/III */
#define ST_HEAD_LOADED 0x20
#define ST_WPROT       0x40
#define ST_NOT_READY   0x80

enum {
    S_IDLE = 0,
    S_TYPE1,          /* aguardando fim do posicionamento */
    S_READ,           /* transferindo setor para a CPU */
    S_WRITE,          /* recebendo setor da CPU */
    S_READ_ADDR,
    S_WRITE_TRACK,
    S_FINISH          /* aguardando para sinalizar INTRQ */
};

#define TICKS_PER_SEC   1789772
#define INDEX_PERIOD    (TICKS_PER_SEC / 5)     /* 300 RPM */
#define INDEX_WIDTH     (TICKS_PER_SEC / 250)
#define STEP_DELAY      200
#define FINISH_DELAY    64

static void set_intrq(wd1793 *f, int v)
{
    v = v ? 1 : 0;
    if (f->intrq != v) {
        f->intrq = v;
        if (f->intrq_changed) f->intrq_changed(f->ctx, v);
    }
}

static void set_drq(wd1793 *f, int v)
{
    v = v ? 1 : 0;
    if (f->drq != v) {
        f->drq = v;
        if (f->drq_changed) f->drq_changed(f->ctx, v);
    }
}

static unsigned drive_status(wd1793 *f)
{
    if (f->drive < 0 || !f->host || !f->host->disk_status)
        return 0;
    return f->host->disk_status(f->host->user, f->drive);
}

static void finish(wd1793 *f, uint8_t status_bits)
{
    /* BUSY permanece ate o INTRQ ser sinalizado em wd1793_tick() */
    f->status = (uint8_t)((f->status & ~ST_DRQ) | status_bits);
    set_drq(f, 0);
    f->state = S_FINISH;
    f->delay = FINISH_DELAY;
}

void wd1793_init(wd1793 *fdc, const coco_host *host, void *ctx)
{
    memset(fdc, 0, sizeof(*fdc));
    fdc->host = host;
    fdc->ctx = ctx;
    fdc->drive = -1;
    wd1793_reset(fdc);
}

void wd1793_reset(wd1793 *f)
{
    f->status = 0;
    f->track = 0;
    f->sector = 1;
    f->data = 0;
    f->command = 0;
    f->state = S_IDLE;
    f->cmd_type = 1;
    f->step_dir = 1;
    f->delay = 0;
    set_drq(f, 0);
    set_intrq(f, 0);
}

void wd1793_select(wd1793 *f, int drive, int side, int dden)
{
    f->drive = drive;
    f->side = side;
    f->dden = dden;
}

static int head(wd1793 *f)
{
    return (f->drive >= 0) ? f->head_pos[f->drive] : 0;
}

static void move_head(wd1793 *f, int dir)
{
    if (f->drive < 0) return;
    f->head_pos[f->drive] += dir;
    if (f->head_pos[f->drive] < 0)  f->head_pos[f->drive] = 0;
    if (f->head_pos[f->drive] > 85) f->head_pos[f->drive] = 85;
}

/* ------------------------------------------------------------------------ */

static int load_sector(wd1793 *f)
{
    unsigned st = drive_status(f);
    if (!(st & COCO_DISK_PRESENT) || !f->host->disk_read_sector)
        return -1;
    if (f->track != head(f))
        return -1;
    if (f->host->disk_read_sector(f->host->user, f->drive, head(f), f->side,
                                  f->sector, f->buf, 256) < 0)
        return -1;
    f->pos = 0;
    f->len = 256;
    return 0;
}

static void cmd_type1(wd1793 *f, uint8_t cmd)
{
    int update = 1;
    int verify = (cmd & 0x04) != 0;

    f->cmd_type = 1;
    f->status = ST_BUSY;
    if (cmd & 0x08) f->status |= ST_HEAD_LOADED;

    switch (cmd >> 4) {
    case 0x0:                                   /* RESTORE */
        if (f->drive >= 0) f->head_pos[f->drive] = 0;
        f->track = 0;
        break;
    case 0x1: {                                 /* SEEK */
        int diff = (int)f->data - (int)f->track;
        if (diff) f->step_dir = diff > 0 ? 1 : -1;
        while (diff > 0) { move_head(f, 1);  diff--; }
        while (diff < 0) { move_head(f, -1); diff++; }
        f->track = f->data;
        break;
    }
    case 0x2: case 0x3:                         /* STEP */
        update = (cmd & 0x10) != 0;
        move_head(f, f->step_dir);
        if (update) f->track = (uint8_t)(f->track + f->step_dir);
        break;
    case 0x4: case 0x5:                         /* STEP IN */
        update = (cmd & 0x10) != 0;
        f->step_dir = 1;
        move_head(f, 1);
        if (update) f->track++;
        break;
    default:                                    /* STEP OUT */
        update = (cmd & 0x10) != 0;
        f->step_dir = -1;
        move_head(f, -1);
        if (update) f->track--;
        break;
    }
    (void)update;

    if (verify && f->track != head(f))
        f->status |= ST_SEEK_ERR;

    f->state = S_TYPE1;
    f->delay = STEP_DELAY;
}

static void cmd_read_sector(wd1793 *f)
{
    f->cmd_type = 2;
    f->status = ST_BUSY;
    if (!(drive_status(f) & COCO_DISK_PRESENT)) {
        finish(f, ST_NOT_READY);
        return;
    }
    if (load_sector(f) < 0) {
        finish(f, ST_RNF);
        return;
    }
    f->state = S_READ;
    f->data = f->buf[0];
    f->status |= ST_DRQ;
    set_drq(f, 1);
}

static void cmd_write_sector(wd1793 *f)
{
    unsigned st = drive_status(f);
    f->cmd_type = 2;
    f->status = ST_BUSY;
    if (!(st & COCO_DISK_PRESENT)) { finish(f, ST_NOT_READY); return; }
    if (st & COCO_DISK_WRITE_PROTECT) { finish(f, ST_WPROT); return; }
    if (f->track != head(f)) { finish(f, ST_RNF); return; }
    f->state = S_WRITE;
    f->pos = 0;
    f->len = 256;
    f->status |= ST_DRQ;
    set_drq(f, 1);
}

static void cmd_read_address(wd1793 *f)
{
    f->cmd_type = 3;
    f->status = ST_BUSY;
    if (!(drive_status(f) & COCO_DISK_PRESENT)) { finish(f, ST_NOT_READY); return; }
    f->buf[0] = (uint8_t)head(f);
    f->buf[1] = (uint8_t)f->side;
    f->buf[2] = 1;
    f->buf[3] = 1;      /* 256 bytes */
    f->buf[4] = 0;
    f->buf[5] = 0;
    f->pos = 0;
    f->len = 6;
    f->sector = f->buf[0];  /* peculiaridade do WD179x */
    f->state = S_READ_ADDR;
    f->data = f->buf[0];
    f->status |= ST_DRQ;
    set_drq(f, 1);
}

static void cmd_write_track(wd1793 *f)
{
    unsigned st = drive_status(f);
    f->cmd_type = 3;
    f->status = ST_BUSY;
    if (!(st & COCO_DISK_PRESENT)) { finish(f, ST_NOT_READY); return; }
    if (st & COCO_DISK_WRITE_PROTECT) { finish(f, ST_WPROT); return; }
    f->state = S_WRITE_TRACK;
    f->pos = 0;
    f->len = f->dden ? 6250 : 3125;
    f->status |= ST_DRQ;
    set_drq(f, 1);
}

/* Interpreta os dados brutos da formatacao e grava os setores encontrados. */
static void commit_track(wd1793 *f)
{
    int i = 0, n = f->pos;
    while (i < n) {
        if (f->buf[i] == 0xfe && i + 4 < n) {
            int trk = f->buf[i + 1];
            int sid = f->buf[i + 2];
            int sec = f->buf[i + 3];
            int size = 128 << (f->buf[i + 4] & 3);
            int j = i + 5;
            (void)trk;
            while (j < n && f->buf[j] != 0xfb) j++;
            if (j + size < n && f->host->disk_write_sector) {
                f->host->disk_write_sector(f->host->user, f->drive, head(f),
                                           f->side ? f->side : sid, sec,
                                           &f->buf[j + 1], size);
                i = j + 1 + size;
                continue;
            }
            i = j;
        }
        i++;
    }
}

static void cmd_force_interrupt(wd1793 *f, uint8_t cmd)
{
    f->state = S_IDLE;
    f->delay = 0;
    set_drq(f, 0);
    f->status &= (uint8_t)~(ST_BUSY | ST_DRQ);
    f->cmd_type = 1;
    if (cmd & 0x0f)
        set_intrq(f, 1);
}

/* ------------------------------------------------------------------------ */

uint8_t wd1793_read(wd1793 *f, int reg)
{
    switch (reg & 3) {
    case 0: {
        uint8_t st = f->status;
        unsigned ds = drive_status(f);
        set_intrq(f, 0);
        if (f->cmd_type == 1) {
            st &= (uint8_t)~(ST_INDEX | ST_TRACK0 | ST_WPROT | ST_NOT_READY);
            if (head(f) == 0) st |= ST_TRACK0;
            if ((ds & COCO_DISK_PRESENT) && (f->time % INDEX_PERIOD) < INDEX_WIDTH)
                st |= ST_INDEX;
            if (ds & COCO_DISK_WRITE_PROTECT) st |= ST_WPROT;
        }
        if (!(ds & COCO_DISK_PRESENT)) st |= ST_NOT_READY;
        return st;
    }
    case 1:
        return f->track;
    case 2:
        return f->sector;
    default:
        if (f->drq) {
            uint8_t v = f->data;
            set_drq(f, 0);
            f->status &= (uint8_t)~ST_DRQ;
            if (f->state == S_READ || f->state == S_READ_ADDR) {
                f->pos++;
                if (f->pos < f->len) {
                    f->data = f->buf[f->pos];
                    f->status |= ST_DRQ;
                    set_drq(f, 1);
                } else if (f->state == S_READ && (f->command & 0x10)) {
                    /* leitura de multiplos setores */
                    f->sector++;
                    if (load_sector(f) == 0) {
                        f->data = f->buf[0];
                        f->status |= ST_DRQ;
                        set_drq(f, 1);
                    } else {
                        finish(f, ST_RNF);
                    }
                } else {
                    finish(f, 0);
                }
            }
            return v;
        }
        return f->data;
    }
}

void wd1793_write(wd1793 *f, int reg, uint8_t val)
{
    switch (reg & 3) {
    case 0:
        set_intrq(f, 0);
        if ((val & 0xf0) == 0xd0) {
            cmd_force_interrupt(f, val);
            return;
        }
        if (f->status & ST_BUSY)
            return;
        f->command = val;
        switch (val >> 4) {
        case 0x0: case 0x1: case 0x2: case 0x3:
        case 0x4: case 0x5: case 0x6: case 0x7:
            cmd_type1(f, val);
            break;
        case 0x8: case 0x9:
            cmd_read_sector(f);
            break;
        case 0xa: case 0xb:
            cmd_write_sector(f);
            break;
        case 0xc:
            cmd_read_address(f);
            break;
        case 0xe:                               /* READ TRACK: nao suportado */
            f->cmd_type = 3;
            f->status = ST_BUSY;
            finish(f, ST_RNF);
            break;
        case 0xf:
            cmd_write_track(f);
            break;
        }
        break;
    case 1:
        f->track = val;
        break;
    case 2:
        f->sector = val;
        break;
    default:
        f->data = val;
        if (f->drq && (f->state == S_WRITE || f->state == S_WRITE_TRACK)) {
            set_drq(f, 0);
            f->status &= (uint8_t)~ST_DRQ;
            f->buf[f->pos++] = val;
            if (f->pos < f->len) {
                f->status |= ST_DRQ;
                set_drq(f, 1);
            } else if (f->state == S_WRITE) {
                int r = -1;
                if (f->host->disk_write_sector)
                    r = f->host->disk_write_sector(f->host->user, f->drive, head(f),
                                                   f->side, f->sector, f->buf, 256);
                if (r < 0) {
                    finish(f, ST_RNF);
                } else if (f->command & 0x10) {
                    f->sector++;
                    f->pos = 0;
                    f->status |= ST_DRQ;
                    set_drq(f, 1);
                } else {
                    finish(f, 0);
                }
            } else {
                commit_track(f);
                finish(f, 0);
            }
        }
        break;
    }
}

void wd1793_tick(wd1793 *f, int ticks)
{
    f->time += (uint64_t)ticks;
    if (f->delay <= 0)
        return;
    f->delay -= ticks;
    if (f->delay > 0)
        return;
    f->delay = 0;
    switch (f->state) {
    case S_TYPE1:
    case S_FINISH:
        f->status &= (uint8_t)~ST_BUSY;
        f->state = S_IDLE;
        set_intrq(f, 1);
        break;
    default:
        break;
    }
}
