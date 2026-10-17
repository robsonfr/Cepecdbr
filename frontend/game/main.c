/*
 * main.c - Frontend de jogo embarcado: um unico executavel que contem o
 * emulador e o binario do jogo, e roda SEM as ROMs da Tandy.
 *
 * O jogo (cartucho em $C000) e convertido em C por tools/bin2c no build
 * (veja o alvo "game" do Makefile).  A maquina recebe um "stub de boot"
 * (coco_load_boot_stub) cujo vetor de RESET salta direto para $C000.
 *
 * Teclas:
 *   F2 / F3    salva / carrega o estado do jogo
 *   F11        tela cheia
 *   F12        reinicia o jogo
 *
 * Opcoes: --scale N  --no-audio  --mouse  --dev (habilita F8-F10)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdl_app.h"

/* gerados por tools/bin2c */
extern const unsigned char game_rom[];
extern const unsigned long game_rom_size;
extern const char game_title[];
extern const char game_id[];

#define CART_BASE  0xc000
#define CART_MAX   (0xff00 - CART_BASE)     /* $C000-$FEFF */

int main(int argc, char **argv)
{
    static sdl_app a;
    coco_host host;
    int i, scale = 3, audio = 1;
    char *pref;

    a.cfg.model = COCO_MODEL_2;
    a.cfg.ram_kb = 64;
    a.cfg.audio_rate = SDL_APP_AUDIO_RATE;
    a.cfg.artifact = COCO_ARTIFACT_OFF;
    a.keys = SDL_APP_KEY_SNAPSHOT | SDL_APP_KEY_FULLSCR | SDL_APP_KEY_RESET;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "--scale") && i + 1 < argc) {
            scale = atoi(argv[++i]);
            if (scale < 1) scale = 1;
        } else if (!strcmp(argv[i], "--no-audio")) {
            audio = 0;
        } else if (!strcmp(argv[i], "--mouse")) {
            a.use_mouse = 1;
        } else if (!strcmp(argv[i], "--dev")) {
            a.keys = SDL_APP_KEY_ALL;
        }
    }

    if (game_rom_size > CART_MAX)
        fprintf(stderr, "Aviso: jogo com %lu bytes; so os primeiros %d cabem em $C000-$FEFF\n",
                game_rom_size, CART_MAX);

    if (sdl_app_open(&a, game_title, scale, audio) != 0)
        return 1;

    /* estados salvos na pasta de preferencias do usuario
     * (Windows: %APPDATA%\Cepecdbr\<id>\, Linux: ~/.local/share/Cepecdbr/<id>/) */
    pref = SDL_GetPrefPath("Cepecdbr", game_id);
    snprintf(a.snapshot_path, sizeof(a.snapshot_path), "%s%s.snap",
             pref ? pref : "", game_id);
    SDL_free(pref);

    sdl_app_host(&a, &host);
    a.m = coco_create(&a.cfg, &host);
    if (!a.m) {
        sdl_app_close(&a);
        return 1;
    }
    coco_load_boot_stub(a.m, CART_BASE);
    coco_load_rom(a.m, COCO_ROM_CARTRIDGE, game_rom, (size_t)game_rom_size);
    coco_reset(a.m, 1);

    sdl_app_run(&a);
    sdl_app_close(&a);
    return 0;
}
