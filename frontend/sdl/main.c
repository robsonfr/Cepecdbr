/*
 * main.c - Frontend SDL2 do emulador TRS-80 Color Computer 1/2.
 *
 * A camada SDL (janela, audio, teclado, snapshots) fica em sdl_app.c;
 * aqui so ficam a linha de comando e a montagem da maquina.
 *
 * Teclas especiais:
 *   F2 / F3    salva / carrega o estado (snapshot)
 *   F8         alterna cores de artefato (PMODE 4)
 *   F9         rebobina a fita
 *   F10        modo turbo (sem limite de velocidade)
 *   F11        tela cheia
 *   F12        reset (Shift+F12: reset a frio)
 *   Esc        BREAK      Home/Del  CLEAR      Backspace  seta esquerda
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sdl_app.h"

static void usage(const char *prog)
{
    fprintf(stderr,
        "Uso: %s [opcoes]\n"
        "  -m, --model 1|2        modelo (padrao 2)\n"
        "  -r, --ram KB           RAM: 4, 16, 32, 64 (padrao: 32 no CoCo1, 64 no CoCo2)\n"
        "  --rom-dir DIR          diretorio das ROMs (padrao ./roms)\n"
        "  --no-ext               sem Extended Color BASIC\n"
        "  --no-disk              sem controlador de disco / Disk BASIC\n"
        "  --cart ARQ             cartucho ROM em $C000 (com autostart)\n"
        "  --no-rom               nao usa ROMs da Tandy: RESET salta direto para o\n"
        "                         cartucho (exige --cart; veja examples/game)\n"
        "  -0..-3 ARQ, --diskN    imagem de disco JVC/DSK na unidade N\n"
        "  --cas ARQ              fita .CAS para leitura (CLOAD/CLOADM)\n"
        "  --cas-out ARQ          grava a saida de fita (CSAVE) em .CAS\n"
        "  --snapshot ARQ         arquivo de estado para F2/F3 (padrao coco.snap)\n"
        "  --artifact 0|1|2       cores de artefato: desligado, azul-vermelho, vermelho-azul\n"
        "  --scale N              escala da janela (padrao 3)\n"
        "  --mouse                usa o mouse como joystick direito\n"
        "  --no-audio             desliga o audio\n",
        prog);
}

int main(int argc, char **argv)
{
    static sdl_app a;
    coco_host host;
    const char *rom_dir = "roms";
    const char *cart = NULL;
    const char *disks[MEDIA_MAX_DRIVES] = { NULL, NULL, NULL, NULL };
    const char *cas = NULL, *cas_out = NULL;
    int want_ext = 1, want_disk = 1, no_rom = 0, scale = 3, audio = 1;
    int i;

    a.cfg.model = COCO_MODEL_2;
    a.cfg.ram_kb = 0;
    a.cfg.audio_rate = SDL_APP_AUDIO_RATE;
    a.cfg.artifact = COCO_ARTIFACT_OFF;
    a.keys = SDL_APP_KEY_ALL;

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
        } else if (!strcmp(o, "--no-rom")) {
            no_rom = 1;
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
        } else if (!strcmp(o, "--snapshot")) {
            NEED_ARG(); snprintf(a.snapshot_path, sizeof(a.snapshot_path), "%s", v);
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
    if (no_rom && !cart) {
        fprintf(stderr, "--no-rom exige --cart ARQ\n");
        return 1;
    }

    if (sdl_app_open(&a, a.cfg.model == COCO_MODEL_1 ? "TRS-80 Color Computer 1"
                                                     : "TRS-80 Color Computer 2",
                     scale, audio) != 0)
        return 1;

    sdl_app_host(&a, &host);
    a.m = coco_create(&a.cfg, &host);
    if (!a.m) {
        fprintf(stderr, "Falha ao criar a maquina\n");
        sdl_app_close(&a);
        return 1;
    }

    if (no_rom) {
        coco_load_boot_stub(a.m, 0xc000);
    } else if (!(media_load_roms(a.m, rom_dir, want_ext, want_disk && !cart) & 1)) {
        fprintf(stderr,
            "Erro: ROM do Color BASIC nao encontrada em '%s'.\n"
            "Coloque bas12.rom/bas13.rom (8K) e, opcionalmente, extbas11.rom\n"
            "e disk11.rom nesse diretorio (ou use --rom-dir).\n", rom_dir);
        sdl_app_close(&a);
        return 1;
    }
    if (cart) {
        size_t size;
        uint8_t *data = media_read_file(cart, &size);
        if (!data) {
            fprintf(stderr, "Nao foi possivel ler o cartucho %s\n", cart);
            sdl_app_close(&a);
            return 1;
        }
        coco_load_rom(a.m, COCO_ROM_CARTRIDGE, data, size);
        /* sem ROM, o RESET ja salta para o cartucho */
        coco_set_cartridge_autostart(a.m, !no_rom);
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
    sdl_app_run(&a);
    sdl_app_close(&a);
    return 0;
}
