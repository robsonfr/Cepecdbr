/*
 * media.c - ROMs, imagens de disco JVC/DSK e fitas .CAS usando stdio.
 *
 * Formato JVC (.DSK): imagem crua de setores de 256 bytes, ordenados por
 * trilha, face e setor.  Um cabecalho opcional (tamanho = tamanho do
 * arquivo % 256) pode conter: setores/trilha, faces, codigo de tamanho do
 * setor, numero do primeiro setor e atributo.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

#include "media.h"

uint8_t *media_read_file(const char *path, size_t *size)
{
    FILE *f = fopen(path, "rb");
    uint8_t *buf;
    long len;
    if (!f)
        return NULL;
    if (fseek(f, 0, SEEK_END) != 0 || (len = ftell(f)) < 0) {
        fclose(f);
        return NULL;
    }
    rewind(f);
    buf = (uint8_t *)malloc(len > 0 ? (size_t)len : 1);
    if (!buf) {
        fclose(f);
        return NULL;
    }
    if (len > 0 && fread(buf, 1, (size_t)len, f) != (size_t)len) {
        free(buf);
        fclose(f);
        return NULL;
    }
    fclose(f);
    *size = (size_t)len;
    return buf;
}

/* ------------------------------------------------------------------------ */
/* ROMs                                                                     */
/* ------------------------------------------------------------------------ */

static uint8_t *find_rom(const char *dir, const char *const *names, size_t *size, char *found, size_t found_len)
{
    char path[1024];
    int i, variant;
    for (i = 0; names[i]; i++) {
        for (variant = 0; variant < 2; variant++) {
            char name[64];
            size_t k;
            uint8_t *data;
            strncpy(name, names[i], sizeof(name) - 1);
            name[sizeof(name) - 1] = 0;
            if (variant)
                for (k = 0; name[k]; k++) name[k] = (char)toupper((unsigned char)name[k]);
            snprintf(path, sizeof(path), "%s/%s", dir, name);
            data = media_read_file(path, size);
            if (data) {
                if (found) {
                    strncpy(found, path, found_len - 1);
                    found[found_len - 1] = 0;
                }
                return data;
            }
        }
    }
    return NULL;
}

int media_load_roms(coco *m, const char *dir, int want_ext, int want_disk)
{
    static const char *const basic_names[] = {
        "bas13.rom", "bas12.rom", "bas11.rom", "bas10.rom",
        "colorbasic.rom", "color_basic.rom", NULL
    };
    static const char *const ext_names[] = {
        "extbas11.rom", "extbas10.rom", "extbasic.rom", "ext_basic.rom", NULL
    };
    static const char *const combo_names[] = {
        "coco2.rom", "coco.rom", "coco2b.rom", NULL
    };
    static const char *const disk_names[] = {
        "disk11.rom", "disk10.rom", "diskbasic.rom", "disk_basic.rom", NULL
    };
    char found[1024];
    uint8_t *data;
    size_t size;
    int result = 0;

    if (!dir) dir = "roms";

    data = find_rom(dir, basic_names, &size, found, sizeof(found));
    if (data) {
        coco_load_rom(m, COCO_ROM_COLOR_BASIC, data, size);
        fprintf(stderr, "ROM: Color BASIC    <- %s\n", found);
        free(data);
        result |= 1;
    }
    if (want_ext) {
        data = find_rom(dir, ext_names, &size, found, sizeof(found));
        if (data) {
            coco_load_rom(m, COCO_ROM_EXTENDED_BASIC, data, size);
            fprintf(stderr, "ROM: Extended BASIC <- %s\n", found);
            free(data);
            result |= 2;
        }
    }
    if (!(result & 1) || (want_ext && !(result & 2))) {
        /* imagem combinada de 16K: Extended ($8000) + Color BASIC ($A000) */
        data = find_rom(dir, combo_names, &size, found, sizeof(found));
        if (data && size >= 0x4000) {
            if (want_ext) {
                coco_load_rom(m, COCO_ROM_EXTENDED_BASIC, data, 0x2000);
                result |= 2;
            }
            coco_load_rom(m, COCO_ROM_COLOR_BASIC, data + 0x2000, 0x2000);
            result |= 1;
            fprintf(stderr, "ROM: Extended+Color <- %s\n", found);
        }
        free(data);
    }
    if (want_disk) {
        data = find_rom(dir, disk_names, &size, found, sizeof(found));
        if (data) {
            coco_load_rom(m, COCO_ROM_CARTRIDGE, data, size);
            coco_set_cartridge_autostart(m, 0);
            coco_enable_disk_controller(m, 1);
            fprintf(stderr, "ROM: Disk BASIC     <- %s\n", found);
            free(data);
            result |= 4;
        }
    }
    return result;
}

/* ------------------------------------------------------------------------ */
/* Disco                                                                    */
/* ------------------------------------------------------------------------ */

int media_disk_insert(media *md, int drive, const char *path, int write_protect)
{
    disk_image *d;
    uint8_t *data;
    size_t size = 0;

    if (drive < 0 || drive >= MEDIA_MAX_DRIVES)
        return -1;
    media_disk_eject(md, drive);
    d = &md->drives[drive];

    data = media_read_file(path, &size);
    if (!data) {
        /* arquivo inexistente: cria disco vazio (use DSKINI para formatar) */
        FILE *f = fopen(path, "wb");
        if (!f)
            return -1;
        fclose(f);
        data = (uint8_t *)malloc(1);
        size = 0;
        if (!data)
            return -1;
    }

    d->path = (char *)malloc(strlen(path) + 1);
    if (!d->path) {
        free(data);
        return -1;
    }
    strcpy(d->path, path);
    d->data = data;
    d->size = size;
    d->header = size % 256;
    d->spt = 18;
    d->sides = 1;
    d->sector_size = 256;
    d->first_sector = 1;
    d->write_protect = write_protect;

    if (d->header >= 1 && data[0]) d->spt = data[0];
    if (d->header >= 2 && data[1]) d->sides = data[1] > 1 ? 2 : 1;
    if (d->header >= 3)            d->sector_size = 128 << (data[2] & 3);
    if (d->header >= 4)            d->first_sector = data[3];
    if (d->header >= 5 && data[4]) d->write_protect = 1;

    /* sem cabecalho: imagens com 2x mais trilhas que o normal sao dupla face */
    if (d->header == 0 && size > 80u * 18 * 256)
        d->sides = 2;
    return 0;
}

void media_disk_eject(media *md, int drive)
{
    disk_image *d;
    if (drive < 0 || drive >= MEDIA_MAX_DRIVES)
        return;
    d = &md->drives[drive];
    free(d->path);
    free(d->data);
    memset(d, 0, sizeof(*d));
}

static long sector_offset(const disk_image *d, int track, int side, int sector)
{
    long lsn;
    if (side >= d->sides)
        return -1;
    if (sector < d->first_sector || sector >= d->first_sector + d->spt)
        return -1;
    lsn = ((long)track * d->sides + side) * d->spt + (sector - d->first_sector);
    return (long)d->header + lsn * d->sector_size;
}

static unsigned disk_status_cb(void *user, int drive)
{
    media *md = (media *)user;
    unsigned st = 0;
    if (drive < 0 || drive >= MEDIA_MAX_DRIVES || !md->drives[drive].data)
        return 0;
    st |= COCO_DISK_PRESENT;
    if (md->drives[drive].write_protect) st |= COCO_DISK_WRITE_PROTECT;
    if (md->drives[drive].sides > 1)     st |= COCO_DISK_DOUBLE_SIDED;
    return st;
}

static int disk_read_cb(void *user, int drive, int track, int side, int sector,
                        uint8_t *buf, int size)
{
    media *md = (media *)user;
    disk_image *d;
    long off;
    if (drive < 0 || drive >= MEDIA_MAX_DRIVES)
        return -1;
    d = &md->drives[drive];
    if (!d->data)
        return -1;
    off = sector_offset(d, track, side, sector);
    if (off < 0 || (size_t)off + (size_t)d->sector_size > d->size)
        return -1;
    memset(buf, 0, (size_t)size);
    memcpy(buf, d->data + off, (size_t)(size < d->sector_size ? size : d->sector_size));
    return 0;
}

static int disk_write_cb(void *user, int drive, int track, int side, int sector,
                         const uint8_t *buf, int size)
{
    media *md = (media *)user;
    disk_image *d;
    long off;
    size_t need, n;
    FILE *f;
    if (drive < 0 || drive >= MEDIA_MAX_DRIVES)
        return -1;
    d = &md->drives[drive];
    if (!d->data || d->write_protect)
        return -1;
    off = sector_offset(d, track, side, sector);
    if (off < 0)
        return -1;
    need = (size_t)off + (size_t)d->sector_size;
    if (need > d->size) {
        uint8_t *nd = (uint8_t *)realloc(d->data, need);
        if (!nd)
            return -1;
        memset(nd + d->size, 0xff, need - d->size);
        d->data = nd;
        d->size = need;
    }
    n = (size_t)(size < d->sector_size ? size : d->sector_size);
    memcpy(d->data + off, buf, n);

    /* grava imediatamente no arquivo (preenchendo lacunas se crescer) */
    f = fopen(d->path, "r+b");
    if (!f) f = fopen(d->path, "w+b");
    if (f) {
        long start = off;
        if (fseek(f, 0, SEEK_END) == 0) {
            long end = ftell(f);
            if (end >= 0 && end < start) start = end;
        }
        if (fseek(f, start, SEEK_SET) == 0)
            fwrite(d->data + start, 1, need - (size_t)start, f);
        fclose(f);
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Fita                                                                     */
/* ------------------------------------------------------------------------ */

int media_tape_insert(media *md, const char *path)
{
    size_t size;
    uint8_t *data = media_read_file(path, &size);
    if (!data)
        return -1;
    free(md->tape.data);
    md->tape.data = data;
    md->tape.size = size;
    md->tape.pos = 0;
    return 0;
}

int media_tape_output(media *md, const char *path)
{
    FILE *f;
    if (md->tape.out)
        fclose((FILE *)md->tape.out);
    f = fopen(path, "ab");
    md->tape.out = f;
    return f ? 0 : -1;
}

void media_tape_rewind(media *md)
{
    md->tape.pos = 0;
}

static int tape_read_cb(void *user)
{
    media *md = (media *)user;
    if (!md->tape.data || md->tape.pos >= md->tape.size)
        return -1;
    return md->tape.data[md->tape.pos++];
}

static void tape_write_cb(void *user, uint8_t byte)
{
    media *md = (media *)user;
    if (md->tape.out) {
        fputc(byte, (FILE *)md->tape.out);
        fflush((FILE *)md->tape.out);
    }
}

void media_free(media *md)
{
    int i;
    for (i = 0; i < MEDIA_MAX_DRIVES; i++)
        media_disk_eject(md, i);
    free(md->tape.data);
    md->tape.data = NULL;
    if (md->tape.out)
        fclose((FILE *)md->tape.out);
    md->tape.out = NULL;
}

void media_attach(media *md, coco_host *host)
{
    host->user = md;
    host->disk_status = disk_status_cb;
    host->disk_read_sector = disk_read_cb;
    host->disk_write_sector = disk_write_cb;
    host->cassette_read_byte = tape_read_cb;
    host->cassette_write_byte = tape_write_cb;
}
