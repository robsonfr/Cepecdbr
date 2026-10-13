/*
 * media.h - Implementacoes de midia baseadas em arquivos (stdio) que os
 * frontends usam para preencher coco_host: ROMs, imagens de disco
 * JVC/DSK e fitas .CAS.  Nada aqui depende de SDL.
 */
#ifndef MEDIA_H
#define MEDIA_H

#include <stddef.h>
#include <stdint.h>
#include "coco.h"

#define MEDIA_MAX_DRIVES 4

typedef struct disk_image {
    char    *path;
    uint8_t *data;
    size_t   size;
    size_t   header;        /* bytes de cabecalho JVC */
    int      spt;           /* setores por trilha */
    int      sides;
    int      sector_size;
    int      first_sector;
    int      write_protect;
} disk_image;

typedef struct cas_tape {
    uint8_t *data;
    size_t   size;
    size_t   pos;
    void    *out;           /* FILE* para gravacao */
} cas_tape;

typedef struct media {
    disk_image drives[MEDIA_MAX_DRIVES];
    cas_tape   tape;
    void      *app;         /* livre para o frontend (contexto de video/audio) */
} media;

/* Le um arquivo inteiro para a memoria. Retorna NULL em erro. */
uint8_t *media_read_file(const char *path, size_t *size);

/* Procura ROMs em 'dir' com os nomes usuais e carrega na maquina.
 * Retorna bitmask: 1 = Color BASIC, 2 = Extended BASIC, 4 = Disk BASIC. */
int  media_load_roms(coco *m, const char *dir, int want_ext, int want_disk);

int  media_disk_insert(media *md, int drive, const char *path, int write_protect);
void media_disk_eject(media *md, int drive);
int  media_tape_insert(media *md, const char *path);
int  media_tape_output(media *md, const char *path);
void media_tape_rewind(media *md);
void media_free(media *md);

/* Preenche os callbacks de disco/fita de 'host' e faz host->user = md.
 * Os callbacks de video/audio do frontend podem usar md->app. */
void media_attach(media *md, coco_host *host);

#endif /* MEDIA_H */
