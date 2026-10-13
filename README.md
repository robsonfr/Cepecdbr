# Cepecdbr: emulador TRS-80 Color Computer 1 e 2 em C

Emulador do **TRS-80 Color Computer 1 e 2** (CoCo 1/2) escrito 100% em C99.

O **núcleo** (`src/core`) não sabe nada sobre gráficos, som, arquivos ou
sistema operacional. Ele se comunica com o mundo externo só pela
estrutura de callbacks `coco_host` (`include/coco.h`). Os **frontends**
implementam essa interface. Este repositório traz dois:

- `frontend/sdl`: frontend completo com **SDL2** (janela, áudio, teclado, joystick/mouse)
- `frontend/headless`: sem interface, roda N quadros e salva a tela em PPM (usado nos testes)

## Arquitetura

```
include/coco.h              API pública do núcleo + interface coco_host
src/core/
  mc6809.[ch]               CPU Motorola 6809E (instruções completas, ciclos, NMI/FIRQ/IRQ, CWAI/SYNC)
  mc6821.[ch]               PIA 6821 (2 unidades: teclado/joystick/sync e DAC/som/VDG/cassete)
  sam.[ch]                  SAM 6883 (modo do VDG, endereço da tela, mapa de memória, velocidade)
  vdg.[ch], font6847.c      VDG 6847 (texto, SG4, SG6, todos os modos gráficos, artefato NTSC)
  wd1793.[ch]               Controlador de disquete WD1793 (FD-502), trabalhando por setor
  cassette.[ch]             Fita: leitura de .CAS (gera a onda de 1200/2400 Hz) e gravação
  coco.c                    Barramento, mapa de memória, temporização de vídeo, áudio, glue
frontend/common/media.[ch]  ROMs, imagens JVC/DSK e fitas .CAS via stdio (sem SDL)
frontend/sdl/main.c         Frontend SDL2
frontend/headless/main.c    Frontend sem interface
tests/                      Testes da CPU, do FDC e uma ROM de teste gerada por script
```

### Interface com o hospedeiro

```c
typedef struct coco_host {
    void *user;
    void (*video_frame)(void *user, const uint8_t *pixels, int w, int h); /* índices de cor */
    void (*audio_samples)(void *user, const int16_t *samples, int count);  /* PCM mono */
    int  (*cassette_read_byte)(void *user);                                /* -1 = fim */
    void (*cassette_write_byte)(void *user, uint8_t byte);
    void (*cassette_motor)(void *user, int on);
    unsigned (*disk_status)(void *user, int drive);
    int  (*disk_read_sector)(void *user, int drive, int track, int side, int sector, uint8_t *buf, int size);
    int  (*disk_write_sector)(void *user, int drive, int track, int side, int sector, const uint8_t *buf, int size);
} coco_host;
```

O framebuffer tem 320x240 pixels: a área ativa de 256x192 com uma borda em volta.
Cada pixel é um índice de `enum coco_color`, e o frontend converte para RGB
(`coco_palette_rgb()` traz uma paleta padrão). O disco é endereçado por
trilha/face/setor físicos, então o formato da imagem (JVC, VDK, DMK...) fica
por conta do hospedeiro. Para portar para outra plataforma (libretro,
framebuffer de microcontrolador, WebAssembly...), basta escrever um novo frontend.

Uso mínimo do núcleo:

```c
coco_config cfg = { COCO_MODEL_2, 64, 44100, COCO_ARTIFACT_OFF };
coco_host host = { .user = ctx, .video_frame = meu_video, .audio_samples = meu_audio };
coco *m = coco_create(&cfg, &host);
coco_load_rom(m, COCO_ROM_COLOR_BASIC, bas, 8192);
coco_load_rom(m, COCO_ROM_EXTENDED_BASIC, extbas, 8192);
coco_reset(m, 1);
for (;;) {
    coco_key_set(m, COCO_KEY_A, tecla_a_pressionada);
    coco_run_frame(m);          /* 1/60 s de máquina emulada */
}
```

## Compilação

Requisitos: compilador C99. Para o frontend gráfico, SDL2 (`libsdl2-dev` no Debian/Ubuntu,
`brew install sdl2` no macOS).

```sh
make            # build/coco-sdl e build/coco-headless
make test       # testes da CPU, do FDC e da ROM de teste
```

Também dá para usar CMake (inclusive no Windows):

```sh
cmake -S . -B build && cmake --build build && ctest --test-dir build
```

## ROMs

As ROMs da Tandy/Microsoft **não** acompanham o projeto. Coloque as suas
em `./roms` (ou indique outro lugar com `--rom-dir`):

| Arquivo                         | Conteúdo                         | Endereço |
|---------------------------------|----------------------------------|----------|
| `bas12.rom` / `bas13.rom`       | Color BASIC (8K), obrigatória    | `$A000`  |
| `extbas11.rom` / `extbas10.rom` | Extended Color BASIC (8K)        | `$8000`  |
| `disk11.rom` / `disk10.rom`     | Disk BASIC (8K), ativa o FD-502  | `$C000`  |
| `coco2.rom` (alternativa)       | Extended + Color BASIC (16K)     | `$8000`  |

## Uso (frontend SDL)

```sh
./build/coco-sdl                         # CoCo 2, 64K, Extended + Disk BASIC (se houver ROMs)
./build/coco-sdl -m 1 -r 16 --no-disk    # CoCo 1 com 16K
./build/coco-sdl -0 jogos.dsk -1 dados.dsk
./build/coco-sdl --cas programa.cas      # depois: CLOAD ou CLOADM
./build/coco-sdl --cas-out saida.cas     # CSAVE grava em saida.cas
./build/coco-sdl --cart jogo.rom         # cartucho com autostart
./build/coco-sdl --artifact 1            # cores de artefato em PMODE 4
```

| Tecla do PC       | Função                             |
|-------------------|------------------------------------|
| Esc / End         | BREAK                              |
| Home / Delete     | CLEAR                              |
| Backspace         | seta para a esquerda               |
| F8                | alterna as cores de artefato       |
| F9                | rebobina a fita                    |
| F10               | turbo (sem limite de velocidade)   |
| F11               | tela cheia                         |
| F12 / Shift+F12   | reset / reset a frio               |

Os símbolos seguem o layout US do PC e são traduzidos para o layout do CoCo
(por exemplo, `"` no PC vira SHIFT+2 no CoCo e `=` vira SHIFT+`-`). Um
controle (gamepad) vira o joystick direito, e um segundo controle, o esquerdo.
Com `--mouse`, o mouse faz o papel do joystick direito.

## O que está emulado

- CPU 6809E com todas as instruções documentadas e contagem de ciclos
- SAM: modos de vídeo, endereço da tela, mapa de memória 0/1 (64K em RAM total), página P1, modo de velocidade dupla
- VDG: texto, SG4, SG6, CG1/CG2/CG3/CG6, RG1/RG2/RG3/RG6, CSS e bordas; renderização linha a linha, então trocas de modo no meio da tela aparecem
- Teclado em matriz, 2 joysticks analógicos (comparador com o DAC) e botões
- Som: DAC de 6 bits, multiplexador de som, som de 1 bit, áudio da fita
- Interrupções HS/FS (PIA0) e CART (PIA1) para autostart de cartucho
- Fita: leitura de .CAS e gravação de .CAS a partir da saída do DAC
- Disco: WD1793 com seek/step/restore, leitura e gravação de setores (simples e múltiplos), read address, formatação (DSKINI) e o latch $FF40 com HALT/NMI, como o Disk BASIC espera
- Imagens JVC/DSK com ou sem cabeçalho, face simples ou dupla; gravações vão direto para o arquivo

### Limitações e próximos passos

- Ainda não testado com as ROMs reais da Tandy, que não estão neste repositório. Os testes usam programas 6809 montados à mão.
- VDG 6847T1 (minúsculas do CoCo 2B) não emulado
- Fitas em WAV, RS-232/impressora e Orchestra-90 não emulados
- Temporização do SAM aproximada: o modo de velocidade dependente do endereço (R0=1) roda em velocidade normal
- Artefato NTSC simplificado (pares de pixels)
