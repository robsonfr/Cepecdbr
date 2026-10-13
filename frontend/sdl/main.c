/*
 * main.c - Frontend SDL2 do emulador TRS-80 Color Computer 1/2.
 *
 * Toda a parte especifica de plataforma vive aqui: janela, textura,
 * audio, teclado, joystick/mouse.  O nucleo (src/core) so enxerga a
 * interface coco_host.
 *
 * Teclas especiais:
 *   F12        reset (Shift+F12: reset a frio)
 *   F11        tela cheia
 *   F10        modo turbo (sem limite de velocidade)
 *   F9         rebobina a fita
 *   F8         alterna cores de artefato (PMODE 4)
 *   Esc        BREAK      Home/Del  CLEAR      Backspace  seta esquerda
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <SDL.h>

#include "coco.h"
#include "media.h"

#define FRAME_HZ      59.923
#define AUDIO_RATE    44100

typedef struct app {
    SDL_Window   *window;
    SDL_Renderer *renderer;
    SDL_Texture  *texture;
    SDL_AudioDeviceID audio;
    SDL_GameController *pads[2];
    uint32_t      palette[COCO_NUM_COLORS];
    coco         *m;
    media         md;
    coco_config   cfg;
    int           turbo;
    int           fullscreen;
    int           running;
    int           use_mouse;

    /* teclado */
    int           pc_shift;
    int           shift_override;   /* -1 nenhum, 0 forca solto, 1 forca pressionado */
    SDL_Scancode  override_owner;
    int           pressed_key[SDL_NUM_SCANCODES];
} app;

/* ------------------------------------------------------------------------ */
/* Callbacks de video e audio                                               */
/* ------------------------------------------------------------------------ */

static void video_frame(void *user, const uint8_t *pixels, int w, int h)
{
    app *a = (app *)((media *)user)->app;
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
    app *a = (app *)((media *)user)->app;
    if (!a->audio || a->turbo)
        return;
    /* evita acumular latencia se o emulador adiantar demais */
    if (SDL_GetQueuedAudioSize(a->audio) > (Uint32)(AUDIO_RATE * 2 / 4))
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

static void update_shift(app *a)
{
    int shift = a->pc_shift;
    if (a->shift_override >= 0)
        shift = a->shift_override;
    coco_key_set(a->m, COCO_KEY_SHIFT, shift);
}

static void handle_key(app *a, SDL_Scancode sc, int down)
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

/* ------------------------------------------------------------------------ */
/* Joystick                                                                 */
/* ------------------------------------------------------------------------ */

static void open_pads(app *a)
{
    int i, n = 0;
    for (i = 0; i < SDL_NumJoysticks() && n < 2; i++) {
        if (SDL_IsGameController(i)) {
            a->pads[n] = SDL_GameControllerOpen(i);
            if (a->pads[n]) n++;
        }
    }
}

static void update_joysticks(app *a)
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
/* Linha de comando                                                         */
/* ------------------------------------------------------------------------ */

static void usage(const char *prog)
{
    fprintf(stderr,
        "Uso: %s [opcoes]\n"
        "  -m, --model 1|2        modelo (padrao 2)\n"
        "  -r, --ram KB           RAM: 4, 16, 32, 64 (padrao: 32 no CoCo1, 64 no CoCo2)\n"
        "  --rom-dir DIR          diretorio das ROMs (padrao ./roms)\n"
        "  --no-ext               sem Extended Color BASIC\n"
        "  --no-disk              sem controlador de disco / Disk BASIC\n"
        "  --cart ARQ             cartucho ROM (com autostart)\n"
        "  -0..-3 ARQ, --diskN    imagem de disco JVC/DSK na unidade N\n"
        "  --cas ARQ              fita .CAS para leitura (CLOAD/CLOADM)\n"
        "  --cas-out ARQ          grava a saida de fita (CSAVE) em .CAS\n"
        "  --artifact 0|1|2       cores de artefato: desligado, azul-vermelho, vermelho-azul\n"
        "  --scale N              escala da janela (padrao 3)\n"
        "  --mouse                usa o mouse como joystick direito\n"
        "  --no-audio             desliga o audio\n",
        prog);
}

int main(int argc, char **argv)
{
    app a;
    coco_host host;
    const char *rom_dir = "roms";
    const char *cart = NULL;
    const char *disks[MEDIA_MAX_DRIVES] = { NULL, NULL, NULL, NULL };
    const char *cas = NULL, *cas_out = NULL;
    int want_ext = 1, want_disk = 1, scale = 3, audio = 1;
    int i, roms;
    double frame_ticks;
    Uint64 next_frame;

    memset(&a, 0, sizeof(a));
    a.cfg.model = COCO_MODEL_2;
    a.cfg.ram_kb = 0;
    a.cfg.audio_rate = AUDIO_RATE;
    a.cfg.artifact = COCO_ARTIFACT_OFF;
    a.shift_override = -1;

    for (i = 1; i < argc; i++) {
        const char *o = argv[i];
        const char *v = (i + 1 < argc) ? argv[i + 1] : NULL;
#define NEED_ARG() do { if (!v) { usage(argv[0]); return 1; } i++; } while (0)
        if (!strcmp(o, "-m") || !strcmp(o, "--model")) {
            NEED_ARG(); a.cfg.model = atoi(v) == 1 ? COCO_MODEL_1 : COCO_MODEL_2;
        } else if (!strcmp(o, "-r") || !strcmp(o, "--ram")) {
            NEED_ARG(); a.cfg.ram_kb = atoi(v);
        } else if (!strcmp(o, "--rom-dir")) {
            NEED_ARG(); rom_dir = v;
        } else if (!strcmp(o, "--no-ext")) {
            want_ext = 0;
        } else if (!strcmp(o, "--no-disk")) {
            want_disk = 0;
        } else if (!strcmp(o, "--cart")) {
            NEED_ARG(); cart = v; want_disk = 0;
        } else if ((o[0] == '-' && o[1] >= '0' && o[1] <= '3' && !o[2]) ||
                   (!strncmp(o, "--disk", 6) && o[6] >= '0' && o[6] <= '3' && !o[7])) {
            int d = (o[1] == '-') ? o[6] - '0' : o[1] - '0';
            NEED_ARG(); disks[d] = v;
        } else if (!strcmp(o, "--cas")) {
            NEED_ARG(); cas = v;
        } else if (!strcmp(o, "--cas-out")) {
            NEED_ARG(); cas_out = v;
        } else if (!strcmp(o, "--artifact")) {
            NEED_ARG(); a.cfg.artifact = (coco_artifact)(atoi(v) % 3);
        } else if (!strcmp(o, "--scale")) {
            NEED_ARG(); scale = atoi(v); if (scale < 1) scale = 1;
        } else if (!strcmp(o, "--mouse")) {
            a.use_mouse = 1;
        } else if (!strcmp(o, "--no-audio")) {
            audio = 0;
        } else if (!strcmp(o, "-h") || !strcmp(o, "--help")) {
            usage(argv[0]);
            return 0;
        } else {
            fprintf(stderr, "Opcao desconhecida: %s\n", o);
            usage(argv[0]);
            return 1;
        }
#undef NEED_ARG
    }
    if (!audio) a.cfg.audio_rate = 0;

    /* ---- SDL ---- */
    if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS | SDL_INIT_GAMECONTROLLER |
                 (audio ? SDL_INIT_AUDIO : 0)) != 0) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    a.window = SDL_CreateWindow(a.cfg.model == COCO_MODEL_1 ? "TRS-80 Color Computer 1"
                                                            : "TRS-80 Color Computer 2",
                                SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED,
                                COCO_SCREEN_W * scale, COCO_SCREEN_H * scale,
                                SDL_WINDOW_RESIZABLE);
    if (!a.window) {
        fprintf(stderr, "SDL_CreateWindow: %s\n", SDL_GetError());
        return 1;
    }
    a.renderer = SDL_CreateRenderer(a.window, -1, SDL_RENDERER_ACCELERATED);
    if (!a.renderer)
        a.renderer = SDL_CreateRenderer(a.window, -1, 0);
    if (!a.renderer) {
        fprintf(stderr, "SDL_CreateRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_RenderSetLogicalSize(a.renderer, COCO_SCREEN_W, COCO_SCREEN_H);
    a.texture = SDL_CreateTexture(a.renderer, SDL_PIXELFORMAT_ARGB8888,
                                  SDL_TEXTUREACCESS_STREAMING,
                                  COCO_SCREEN_W, COCO_SCREEN_H);
    for (i = 0; i < COCO_NUM_COLORS; i++)
        a.palette[i] = 0xff000000u | coco_palette_rgb(i);

    if (audio) {
        SDL_AudioSpec want, have;
        memset(&want, 0, sizeof(want));
        want.freq = AUDIO_RATE;
        want.format = AUDIO_S16SYS;
        want.channels = 1;
        want.samples = 1024;
        a.audio = SDL_OpenAudioDevice(NULL, 0, &want, &have, 0);
        if (!a.audio) {
            fprintf(stderr, "Aviso: sem audio (%s)\n", SDL_GetError());
            a.cfg.audio_rate = 0;
        } else {
            SDL_PauseAudioDevice(a.audio, 0);
        }
    }
    open_pads(&a);

    /* ---- maquina ---- */
    memset(&host, 0, sizeof(host));
    a.md.app = &a;
    media_attach(&a.md, &host);
    host.video_frame = video_frame;
    host.audio_samples = audio_samples;

    a.m = coco_create(&a.cfg, &host);
    if (!a.m) {
        fprintf(stderr, "Falha ao criar a maquina\n");
        return 1;
    }
    roms = media_load_roms(a.m, rom_dir, want_ext, want_disk && !cart);
    if (!(roms & 1)) {
        fprintf(stderr,
            "Erro: ROM do Color BASIC nao encontrada em '%s'.\n"
            "Coloque bas12.rom/bas13.rom (8K) e, opcionalmente, extbas11.rom\n"
            "e disk11.rom nesse diretorio (ou use --rom-dir).\n", rom_dir);
        return 1;
    }
    if (cart) {
        size_t size;
        uint8_t *data = media_read_file(cart, &size);
        if (!data) {
            fprintf(stderr, "Nao foi possivel ler o cartucho %s\n", cart);
            return 1;
        }
        coco_load_rom(a.m, COCO_ROM_CARTRIDGE, data, size);
        coco_set_cartridge_autostart(a.m, 1);
        free(data);
    }
    for (i = 0; i < MEDIA_MAX_DRIVES; i++) {
        if (disks[i] && media_disk_insert(&a.md, i, disks[i], 0) != 0)
            fprintf(stderr, "Aviso: nao foi possivel abrir o disco %s\n", disks[i]);
    }
    if (cas && media_tape_insert(&a.md, cas) != 0)
        fprintf(stderr, "Aviso: nao foi possivel abrir a fita %s\n", cas);
    if (cas_out && media_tape_output(&a.md, cas_out) != 0)
        fprintf(stderr, "Aviso: nao foi possivel criar %s\n", cas_out);

    coco_reset(a.m, 1);

    /* ---- laco principal ---- */
    a.running = 1;
    frame_ticks = (double)SDL_GetPerformanceFrequency() / FRAME_HZ;
    next_frame = SDL_GetPerformanceCounter();

    while (a.running) {
        SDL_Event ev;
        while (SDL_PollEvent(&ev)) {
            switch (ev.type) {
            case SDL_QUIT:
                a.running = 0;
                break;
            case SDL_KEYDOWN:
            case SDL_KEYUP: {
                SDL_Scancode sc = ev.key.keysym.scancode;
                int down = ev.type == SDL_KEYDOWN;
                if (down && !ev.key.repeat) {
                    if (sc == SDL_SCANCODE_F12) {
                        coco_reset(a.m, (ev.key.keysym.mod & KMOD_SHIFT) != 0);
                        break;
                    } else if (sc == SDL_SCANCODE_F11) {
                        a.fullscreen = !a.fullscreen;
                        SDL_SetWindowFullscreen(a.window, a.fullscreen ? SDL_WINDOW_FULLSCREEN_DESKTOP : 0);
                        break;
                    } else if (sc == SDL_SCANCODE_F10) {
                        a.turbo = !a.turbo;
                        if (a.audio) SDL_ClearQueuedAudio(a.audio);
                        next_frame = SDL_GetPerformanceCounter();
                        break;
                    } else if (sc == SDL_SCANCODE_F9) {
                        media_tape_rewind(&a.md);
                        coco_cassette_rewind(a.m);
                        fprintf(stderr, "Fita rebobinada\n");
                        break;
                    } else if (sc == SDL_SCANCODE_F8) {
                        a.cfg.artifact = (coco_artifact)((a.cfg.artifact + 1) % 3);
                        coco_set_artifact(a.m, a.cfg.artifact);
                        fprintf(stderr, "Artefato: %d\n", a.cfg.artifact);
                        break;
                    }
                }
                if (down && ev.key.repeat)
                    break;
                handle_key(&a, sc, down);
                break;
            }
            case SDL_WINDOWEVENT:
                if (ev.window.event == SDL_WINDOWEVENT_FOCUS_LOST) {
                    coco_keys_clear(a.m);
                    memset(a.pressed_key, 0, sizeof(a.pressed_key));
                    a.shift_override = -1;
                    a.pc_shift = 0;
                }
                break;
            case SDL_CONTROLLERDEVICEADDED:
            case SDL_CONTROLLERDEVICEREMOVED:
                for (i = 0; i < 2; i++) {
                    if (a.pads[i]) SDL_GameControllerClose(a.pads[i]);
                    a.pads[i] = NULL;
                }
                open_pads(&a);
                break;
            default:
                break;
            }
        }

        update_joysticks(&a);
        coco_run_frame(a.m);

        SDL_RenderClear(a.renderer);
        SDL_RenderCopy(a.renderer, a.texture, NULL, NULL);
        SDL_RenderPresent(a.renderer);

        if (!a.turbo) {
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

    coco_destroy(a.m);
    media_free(&a.md);
    for (i = 0; i < 2; i++)
        if (a.pads[i]) SDL_GameControllerClose(a.pads[i]);
    if (a.audio) SDL_CloseAudioDevice(a.audio);
    SDL_DestroyTexture(a.texture);
    SDL_DestroyRenderer(a.renderer);
    SDL_DestroyWindow(a.window);
    SDL_Quit();
    return 0;
}
