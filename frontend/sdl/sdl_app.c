/*
 * sdl_app.c - Camada SDL2 compartilhada pelos frontends.
 *
 * Toda a parte especifica de plataforma vive aqui: janela, textura,
 * audio, teclado, joystick/mouse.  O nucleo (src/core) so enxerga a
 * interface coco_host.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdl_app.h"

#define FRAME_HZ 59.923

static char base_title[256];
static Uint32 notice_until;

/* Mostra uma mensagem curta na barra de titulo (e no stderr). */
static void notice(sdl_app *a, const char *msg)
{
    char title[512];
    fprintf(stderr, "%s\n", msg);
    snprintf(title, sizeof(title), "%s - %s", base_title, msg);
    SDL_SetWindowTitle(a->window, title);
    notice_until = SDL_GetTicks() + 2500;
}

/* ------------------------------------------------------------------------ */
/* Callbacks de video e audio                                               */
/* ------------------------------------------------------------------------ */

static void video_frame(void *user, const uint8_t *pixels, int w, int h)
{
    sdl_app *a = (sdl_app *)((media *)user)->app;
    void *dst;
    int pitch, x, y;
    if (SDL_LockTexture(a->texture, NULL, &dst, &pitch) != 0)
        return;
    for (y = 0; y < h; y++) {
        uint32_t *row = (uint32_t *)((uint8_t *)dst + y * pitch);
        const uint8_t *src = pixels + y * w;
        for (x = 0; x < w; x++)
            row[x] = a->palette[src[x] < COCO_NUM_COLORS ? src[x] : 0];
    }
    SDL_UnlockTexture(a->texture);
}

static void audio_samples(void *user, const int16_t *samples, int count)
{
    sdl_app *a = (sdl_app *)((media *)user)->app;
    if (!a->audio || a->turbo)
        return;
    /* evita acumular latencia se o emulador adiantar demais */
    if (SDL_GetQueuedAudioSize(a->audio) > (Uint32)(SDL_APP_AUDIO_RATE * 2 / 4))
        return;
    SDL_QueueAudio(a->audio, samples, (Uint32)count * sizeof(int16_t));
}

/* ------------------------------------------------------------------------ */
/* Teclado                                                                  */
/* ------------------------------------------------------------------------ */

typedef struct keymap {
    SDL_Scancode sc;
    int key;
} keymap;

static const keymap direct_keys[] = {
    { SDL_SCANCODE_A, COCO_KEY_A }, { SDL_SCANCODE_B, COCO_KEY_B },
    { SDL_SCANCODE_C, COCO_KEY_C }, { SDL_SCANCODE_D, COCO_KEY_D },
    { SDL_SCANCODE_E, COCO_KEY_E }, { SDL_SCANCODE_F, COCO_KEY_F },
    { SDL_SCANCODE_G, COCO_KEY_G }, { SDL_SCANCODE_H, COCO_KEY_H },
    { SDL_SCANCODE_I, COCO_KEY_I }, { SDL_SCANCODE_J, COCO_KEY_J },
    { SDL_SCANCODE_K, COCO_KEY_K }, { SDL_SCANCODE_L, COCO_KEY_L },
    { SDL_SCANCODE_M, COCO_KEY_M }, { SDL_SCANCODE_N, COCO_KEY_N },
    { SDL_SCANCODE_O, COCO_KEY_O }, { SDL_SCANCODE_P, COCO_KEY_P },
    { SDL_SCANCODE_Q, COCO_KEY_Q }, { SDL_SCANCODE_R, COCO_KEY_R },
    { SDL_SCANCODE_S, COCO_KEY_S }, { SDL_SCANCODE_T, COCO_KEY_T },
    { SDL_SCANCODE_U, COCO_KEY_U }, { SDL_SCANCODE_V, COCO_KEY_V },
    { SDL_SCANCODE_W, COCO_KEY_W }, { SDL_SCANCODE_X, COCO_KEY_X },
    { SDL_SCANCODE_Y, COCO_KEY_Y }, { SDL_SCANCODE_Z, COCO_KEY_Z },
    { SDL_SCANCODE_RETURN, COCO_KEY_ENTER },
    { SDL_SCANCODE_KP_ENTER, COCO_KEY_ENTER },
    { SDL_SCANCODE_SPACE, COCO_KEY_SPACE },
    { SDL_SCANCODE_UP, COCO_KEY_UP },
    { SDL_SCANCODE_DOWN, COCO_KEY_DOWN },
    { SDL_SCANCODE_LEFT, COCO_KEY_LEFT },
    { SDL_SCANCODE_RIGHT, COCO_KEY_RIGHT },
    { SDL_SCANCODE_BACKSPACE, COCO_KEY_LEFT },
    { SDL_SCANCODE_ESCAPE, COCO_KEY_BREAK },
    { SDL_SCANCODE_END, COCO_KEY_BREAK },
    { SDL_SCANCODE_HOME, COCO_KEY_CLEAR },
    { SDL_SCANCODE_DELETE, COCO_KEY_CLEAR },
    { SDL_SCANCODE_KP_0, COCO_KEY_0 }, { SDL_SCANCODE_KP_1, COCO_KEY_1 },
    { SDL_SCANCODE_KP_2, COCO_KEY_2 }, { SDL_SCANCODE_KP_3, COCO_KEY_3 },
    { SDL_SCANCODE_KP_4, COCO_KEY_4 }, { SDL_SCANCODE_KP_5, COCO_KEY_5 },
    { SDL_SCANCODE_KP_6, COCO_KEY_6 }, { SDL_SCANCODE_KP_7, COCO_KEY_7 },
    { SDL_SCANCODE_KP_8, COCO_KEY_8 }, { SDL_SCANCODE_KP_9, COCO_KEY_9 },
    { SDL_SCANCODE_KP_PERIOD, COCO_KEY_PERIOD },
    { SDL_SCANCODE_KP_MINUS, COCO_KEY_MINUS },
    { SDL_SCANCODE_KP_DIVIDE, COCO_KEY_SLASH },
};

/* Teclas de simbolo: traduz o layout do PC (US) para o do CoCo,
 * forcando o estado do SHIFT do CoCo quando necessario. */
typedef struct symmap {
    SDL_Scancode sc;
    int key, shift;          /* PC sem shift */
    int key_s, shift_s;      /* PC com shift */
} symmap;

static const symmap symbol_keys[] = {
    { SDL_SCANCODE_1, COCO_KEY_1, 0, COCO_KEY_1, 1 },              /* 1 ! */
    { SDL_SCANCODE_2, COCO_KEY_2, 0, COCO_KEY_AT, 0 },             /* 2 @ */
    { SDL_SCANCODE_3, COCO_KEY_3, 0, COCO_KEY_3, 1 },              /* 3 # */
    { SDL_SCANCODE_4, COCO_KEY_4, 0, COCO_KEY_4, 1 },              /* 4 $ */
    { SDL_SCANCODE_5, COCO_KEY_5, 0, COCO_KEY_5, 1 },              /* 5 % */
    { SDL_SCANCODE_6, COCO_KEY_6, 0, COCO_KEY_UP, 0 },             /* 6 ^ */
    { SDL_SCANCODE_7, COCO_KEY_7, 0, COCO_KEY_6, 1 },              /* 7 & */
    { SDL_SCANCODE_8, COCO_KEY_8, 0, COCO_KEY_COLON, 1 },          /* 8 * */
    { SDL_SCANCODE_9, COCO_KEY_9, 0, COCO_KEY_8, 1 },              /* 9 ( */
    { SDL_SCANCODE_0, COCO_KEY_0, 0, COCO_KEY_9, 1 },              /* 0 ) */
    { SDL_SCANCODE_MINUS, COCO_KEY_MINUS, 0, COCO_KEY_MINUS, 0 },  /* - _ */
    { SDL_SCANCODE_EQUALS, COCO_KEY_MINUS, 1, COCO_KEY_SEMICOLON, 1 }, /* = + */
    { SDL_SCANCODE_LEFTBRACKET, COCO_KEY_DOWN, 1, COCO_KEY_DOWN, 1 },  /* [ */
    { SDL_SCANCODE_RIGHTBRACKET, COCO_KEY_RIGHT, 1, COCO_KEY_RIGHT, 1 }, /* ] */
    { SDL_SCANCODE_BACKSLASH, COCO_KEY_CLEAR, 1, COCO_KEY_CLEAR, 1 },  /* \ */
    { SDL_SCANCODE_SEMICOLON, COCO_KEY_SEMICOLON, 0, COCO_KEY_COLON, 0 }, /* ; : */
    { SDL_SCANCODE_APOSTROPHE, COCO_KEY_7, 1, COCO_KEY_2, 1 },     /* ' " */
    { SDL_SCANCODE_COMMA, COCO_KEY_COMMA, 0, COCO_KEY_COMMA, 1 },  /* , < */
    { SDL_SCANCODE_PERIOD, COCO_KEY_PERIOD, 0, COCO_KEY_PERIOD, 1 }, /* . > */
    { SDL_SCANCODE_SLASH, COCO_KEY_SLASH, 0, COCO_KEY_SLASH, 1 },  /* / ? */
    { SDL_SCANCODE_KP_PLUS, COCO_KEY_SEMICOLON, 1, COCO_KEY_SEMICOLON, 1 },
    { SDL_SCANCODE_KP_MULTIPLY, COCO_KEY_COLON, 1, COCO_KEY_COLON, 1 },
};

static void update_shift(sdl_app *a)
{
    int shift = a->pc_shift;
    if (a->shift_override >= 0)
        shift = a->shift_override;
    coco_key_set(a->m, COCO_KEY_SHIFT, shift);
}

static void release_all_keys(sdl_app *a)
{
    coco_keys_clear(a->m);
    memset(a->pressed_key, 0, sizeof(a->pressed_key));
    a->shift_override = -1;
    a->pc_shift = 0;
}

static void handle_key(sdl_app *a, SDL_Scancode sc, int down)
{
    size_t i;

    if (sc == SDL_SCANCODE_LSHIFT || sc == SDL_SCANCODE_RSHIFT) {
        const Uint8 *ks = SDL_GetKeyboardState(NULL);
        a->pc_shift = ks[SDL_SCANCODE_LSHIFT] || ks[SDL_SCANCODE_RSHIFT];
        update_shift(a);
        return;
    }

    if (!down) {
        if (a->pressed_key[sc]) {
            coco_key_set(a->m, a->pressed_key[sc] - 1, 0);
            a->pressed_key[sc] = 0;
        }
        if (a->shift_override >= 0 && a->override_owner == sc) {
            a->shift_override = -1;
            update_shift(a);
        }
        return;
    }

    for (i = 0; i < sizeof(direct_keys) / sizeof(direct_keys[0]); i++) {
        if (direct_keys[i].sc == sc) {
            coco_key_set(a->m, direct_keys[i].key, 1);
            a->pressed_key[sc] = direct_keys[i].key + 1;
            return;
        }
    }
    for (i = 0; i < sizeof(symbol_keys) / sizeof(symbol_keys[0]); i++) {
        const symmap *s = &symbol_keys[i];
        if (s->sc == sc) {
            int key = a->pc_shift ? s->key_s : s->key;
            int shift = a->pc_shift ? s->shift_s : s->shift;
            a->shift_override = shift;
            a->override_owner = sc;
            update_shift(a);
            coco_key_set(a->m, key, 1);
            a->pressed_key[sc] = key + 1;
            return;
        }
    }
}

/* Teclas de funcao do frontend. Retorna 1 se a tecla foi consumida. */
static int handle_function_key(sdl_app *a, SDL_Scancode sc, SDL_Keymod mod,
                               Uint64 *next_frame)
{
    if ((a->keys & SDL_APP_KEY_SNAPSHOT) && sc == SDL_SCANCODE_F2) {
        sdl_app_save_snapshot(a);
        return 1;
    }
    if ((a->keys & SDL_APP_KEY_SNAPSHOT) && sc == SDL_SCANCODE_F3) {
        sdl_app_load_snapshot(a);
        return 1;
    }
    if ((a->keys & SDL_APP_KEY_FULLSCR) && sc == SDL_SCANCODE_F11) {
        a->fullscreen = !a->fullscreen;
        SDL_SetWindowFullscreen(a->window, a->fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
        return 1;
    }
    if ((a->keys & SDL_APP_KEY_RESET) && sc == SDL_SCANCODE_F12) {
        coco_reset(a->m, (mod & KMOD_SHIFT) != 0);
        return 1;
    }
    if (!(a->keys & SDL_APP_KEY_DEV))
        return 0;
    if (sc == SDL_SCANCODE_F10) {
        a->turbo = !a->turbo;
        if (a->audio) SDL_ClearQueuedAudio(a->audio);
        *next_frame = SDL_GetPerformanceCounter();
        notice(a, a->turbo ? "turbo ligado" : "turbo desligado");
        return 1;
    }
    if (sc == SDL_SCANCODE_F9) {
        media_tape_rewind(&a->md);
        coco_cassette_rewind(a->m);
        notice(a, "fita rebobinada");
        return 1;
    }
    if (sc == SDL_SCANCODE_F8) {
        static const char *names[] = { "artefato desligado", "artefato azul-vermelho",
                                       "artefato vermelho-azul" };
        a->cfg.artifact = (coco_artifact)((a->cfg.artifact + 1) % 3);
        coco_set_artifact(a->m, a->cfg.artifact);
        notice(a, names[a->cfg.artifact]);
        return 1;
    }
    return 0;
}

/* ------------------------------------------------------------------------ */
/* Joystick                                                                 */
/* ------------------------------------------------------------------------ */

static void open_pads(sdl_app *a)
{
    int i, n = 0;
    for (i = 0; i < 2; i++) {
        if (a->pads[i]) SDL_GameControllerClose(a->pads[i]);
        a->pads[i] = NULL;
    }
    for (i = 0; i < SDL_NumJoysticks() && n < 2; i++) {
        if (SDL_IsGameController(i)) {
            a->pads[n] = SDL_GameControllerOpen(i);
            if (a->pads[n]) n++;
        }
    }
}

static void update_joysticks(sdl_app *a)
{
    int j;
    for (j = 0; j < 2; j++) {
        if (a->pads[j]) {
            int x = SDL_GameControllerGetAxis(a->pads[j], SDL_CONTROLLER_AXIS_LEFTX);
            int y = SDL_GameControllerGetAxis(a->pads[j], SDL_CONTROLLER_AXIS_LEFTY);
            int b = SDL_GameControllerGetButton(a->pads[j], SDL_CONTROLLER_BUTTON_A) ||
                    SDL_GameControllerGetButton(a->pads[j], SDL_CONTROLLER_BUTTON_B);
            coco_joystick_set(a->m, j, (x + 32768) >> 10, (y + 32768) >> 10, b);
        }
    }
    if (a->use_mouse && !a->pads[0]) {
        int mx, my, w, h;
        Uint32 buttons = SDL_GetMouseState(&mx, &my);
        SDL_GetWindowSize(a->window, &w, &h);
        if (w > 0 && h > 0)
            coco_joystick_set(a->m, 0, mx * 64 / w, my * 64 / h,
                              (buttons & SDL_BUTTON_LMASK) != 0);
    }
}

/* ------------------------------------------------------------------------ */
/* Snapshots                                                                */
/* ------------------------------------------------------------------------ */

int sdl_app_save_snapshot(sdl_app *a)
{
    size_t size = coco_snapshot_size(a->m);
    uint8_t *buf = (uint8_t *)malloc(size);
    char msg[1200];
    int ok;
    if (!buf)
        return -1;
    size = coco_snapshot_save(a->m, buf, size);
    ok = size && media_write_file(a->snapshot_path, buf, size) == 0;
    free(buf);
    snprintf(msg, sizeof(msg), ok ? "estado salvo em %s" : "falha ao salvar %s",
             a->snapshot_path);
    notice(a, msg);
    return ok ? 0 : -1;
}

int sdl_app_load_snapshot(sdl_app *a)
{
    size_t size;
    uint8_t *buf = media_read_file(a->snapshot_path, &size);
    char msg[1200];
    int r;
    if (!buf) {
        snprintf(msg, sizeof(msg), "nenhum estado salvo em %s", a->snapshot_path);
        notice(a, msg);
        return -1;
    }
    r = coco_snapshot_load(a->m, buf, size);
    free(buf);
    switch (r) {
    case COCO_SNAPSHOT_OK:        snprintf(msg, sizeof(msg), "estado carregado"); break;
    case COCO_SNAPSHOT_ERR_RAM:   snprintf(msg, sizeof(msg), "estado salvo com outro tamanho de RAM"); break;
    case COCO_SNAPSHOT_ERR_ROM:   snprintf(msg, sizeof(msg), "estado salvo com outras ROMs/outro jogo"); break;
    default:                      snprintf(msg, sizeof(msg), "arquivo de estado invalido"); break;
    }
    notice(a, msg);
    if (r == COCO_SNAPSHOT_OK)
        release_all_keys(a);
    return r == COCO_SNAPSHOT_OK ? 0 : -1;
}

/* ------------------------------------------------------------------------ */
/* Ciclo de vida                                                            */
/* ------------------------------------------------------------------------ */

int sdl_app_open(sdl_app *a, const char *title, int scale, int audio)
{
    int i;

    a->shift_override = -1;
    if (!a->snapshot_path[0])
        snprintf(a->snapshot_path, sizeof(a->snapshot_path), "coco.snap");
    if (!audio)
        a->cfg.audio_rate = 0;

    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER |
                 (audio ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return -1;
    }
    snprintf(base_title, sizeof(base_title), "%s", title);
    a->window = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                 COCO_SCREEN_W * scale, COCO_SCREEN_H * scale,
                                 SDL_WINDOW_RESIZABLE);
    if (!a->window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return -1;
    }
    a->renderer = SDL_CreateRenderer(a->window, -1, SDL_RENDERER_ACCELERATED);
    if (!a->renderer)
        a->renderer = SDL_CreateRenderer(a->window, -1, 0);
    if (!a->renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return -1;
    }
    SDL_RenderSetLogicalSize(a->renderer, COCO_SCREEN_W, COCO_SCREEN_H);
    a->texture = SDL_CreateTexture(a->renderer, SDL_PIXELFORMAT_ARGB8888,
                                   SDL_TEXTUREACCESS_STREAMING,
                                   COCO_SCREEN_W, COCO_SCREEN_H);
    for (i = 0; i < COCO_NUM_COLORS; i++)
        a->palette[i] = 0xff000000u | coco_palette_rgb(i);

    if (audio) {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq = SDL_APP_AUDIO_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        a->audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!a->audio) {
            fprintf(stderr, "Aviso: sem audio (%s)\n", SDL_GetError());
            a->cfg.audio_rate = 0;
        } else {
            a->cfg.audio_rate = SDL_APP_AUDIO_RATE;
            SDL_PauseAudioDevice(a->audio, 0);
        }
    }
    open_pads(a);
    return 0;
}

void sdl_app_host(sdl_app *a, coco_host *host)
{
    memset(host, 0, sizeof(*host));
    a->md.app = a;
    media_attach(&a->md, host);
    host->video_frame = video_frame;
    host->audio_samples = audio_samples;
}

void sdl_app_run(sdl_app *a)
{
    double frame_ticks = (double)SDL_GetPerformanceFrequency() / FRAME_HZ;
    Uint64 next_frame = SDL_GetPerformanceCounter();

    a->running = 1;
    while (a->running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                a->running = 0;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                SDL_Scancode sc = ev.key.keysym.scancode;
                int down = ev.type == SDL_KEYDOWN;
                if (down && ev.key.repeat)
                    break;
                if (down && handle_function_key(a, sc, (SDL_Keymod)ev.key.keysym.mod, &next_frame))
                    break;
                handle_key(a, sc, down);
                break;
            }
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST)
                    release_all_keys(a);
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                open_pads(a);
                break;
            default:
                break;
            }
        }

        if (notice_until && SDL_GetTicks() > notice_until) {
            SDL_SetWindowTitle(a->window, base_title);
            notice_until = 0;
        }

        update_joysticks(a);
        coco_run_frame(a->m);

        SDL_RenderClear(a->renderer);
        SDL_RenderCopy(a->renderer, a->texture, NULL, NULL);
        SDL_RenderPresent(a->renderer);

        if (!a->turbo) {
            Uint64 now;
            next_frame += (Uint64)frame_ticks;
            now = SDL_GetPerformanceCounter();
            if (now < next_frame) {
                Uint32 ms = (Uint32)((next_frame - now) * 1000 / SDL_GetPerformanceFrequency());
                if (ms > 0) SDL_Delay(ms);
            } else if (now - next_frame > (Uint64)(frame_ticks * 10)) {
                next_frame = now;   /* atrasou demais: ressincroniza */
            }
        }
    }
}

void sdl_app_close(sdl_app *a)
{
    int i;
    if (a->m) coco_destroy(a->m);
    a->m = NULL;
    media_free(&a->md);
    for (i = 0; i < 2; i++)
        if (a->pads[i]) SDL_GameControllerClose(a->pads[i]);
    if (a->audio) SDL_CloseAudioDevice(a->audio);
    if (a->texture) SDL_DestroyTexture(a->texture);
    if (a->renderer) SDL_DestroyRenderer(a->renderer);
    if (a->window) SDL_DestroyWindow(a->window);
    SDL_Quit();
}
