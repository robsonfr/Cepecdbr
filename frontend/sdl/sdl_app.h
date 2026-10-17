/*
 * sdl_app.h - Camada SDL2 compartilhada pelos frontends (emulador e jogo
 * embarcado): janela, textura, audio, teclado, joystick, laco principal e
 * snapshots.
 */
#ifndef SDL_APP_H
#define SDL_APP_H

#include <SDL.h>

#include "coco.h"
#include "media.h"

#define SDL_APP_AUDIO_RATE 44100

/* Teclas de funcao habilitadas (combinaveis) */
#define SDL_APP_KEY_SNAPSHOT  0x01   /* F2 salva, F3 carrega */
#define SDL_APP_KEY_FULLSCR   0x02   /* F11 tela cheia */
#define SDL_APP_KEY_RESET     0x04   /* F12 reset, Shift+F12 reset a frio */
#define SDL_APP_KEY_DEV       0x08   /* F8 artefato, F9 fita, F10 turbo */
#define SDL_APP_KEY_ALL       0x0f

typedef struct sdl_app {
    media         md;          /* host->user = &md; md.app = este app */
    coco         *m;
    coco_config   cfg;

    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    SDL_AudioDeviceID audio;
    SDL_GameController *pads[2];
    uint32_t      palette[COCO_NUM_COLORS];

    unsigned      keys;        /* SDL_APP_KEY_* */
    int           use_mouse;
    int           turbo;
    int           fullscreen;
    int           running;
    char          snapshot_path[1024];

    /* teclado */
    int           pc_shift;
    int           shift_override;
    SDL_Scancode  override_owner;
    int           pressed_key[SDL_NUM_SCANCODES];
} sdl_app;

/* Inicializa SDL, janela e audio. a->cfg deve estar preenchido; se o audio
 * nao abrir, a->cfg.audio_rate vira 0. Retorna 0 em sucesso. */
int  sdl_app_open(sdl_app *a, const char *title, int scale, int audio);
/* Preenche os callbacks de video/audio/midia de 'host'. */
void sdl_app_host(sdl_app *a, coco_host *host);
/* Laco principal (retorna quando a janela e fechada). */
void sdl_app_run(sdl_app *a);
void sdl_app_close(sdl_app *a);

/* Snapshots em arquivo (a->snapshot_path). Retornam 0 em sucesso. */
int  sdl_app_save_snapshot(sdl_app *a);
int  sdl_app_load_snapshot(sdl_app *a);

#endif /* SDL_APP_H */
