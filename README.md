# Cepecdbr: emulador TRS-80 Color Computer 1 e 2 em C

Emulador do **TRS-80 Color Computer 1 e 2** (CoCo 1/2) escrito 100% em C99.

O **núcleo** (`src/core`) não sabe nada sobre gráficos, som, arquivos ou
sistema operacional. Ele se comunica com o mundo externo só pela
estrutura de callbacks `coco_host` (`include/coco.h`). Os **frontends**
implementam essa interface. Este repositório traz três:

- `frontend/sdl`: o emulador completo com **SDL2** (janela, áudio, teclado, joystick/mouse, snapshots)
- `frontend/game`: um executável que já traz um jogo embutido e roda **sem as ROMs da Tandy**
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
frontend/sdl/sdl_app.[ch]   Camada SDL2 comum (janela, áudio, teclado, laço, snapshots)
frontend/sdl/main.c         Emulador SDL2 (linha de comando, ROMs, mídias)
frontend/game/main.c        Jogo embarcado: emulador + binário do jogo num só executável
frontend/headless/main.c    Frontend sem interface
examples/game/game.asm      Jogo-exemplo em assembly 6809 que não usa ROM
tools/asm6809.py            Assembler 6809 mínimo (sintaxe do lwasm) usado no build
tools/bin2c.c               Converte o binário do jogo em fonte C
tests/                      Testes da CPU, do FDC, da máquina, do jogo e dos snapshots
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
make            # build/coco-sdl, build/coco-headless e build/coleta (jogo-exemplo)
make test       # testes da CPU, do FDC, da máquina, do jogo e dos snapshots
```

O jogo-exemplo é montado por `tools/asm6809.py`, então o build dele precisa de Python 3.

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
./build/coco-sdl --no-rom --cart jogo.bin  # sem ROMs: o RESET salta direto para o cartucho
./build/coco-sdl --snapshot partida.snap # arquivo usado por F2/F3 (padrão coco.snap)
```

| Tecla do PC       | Função                             |
|-------------------|------------------------------------|
| Esc / End         | BREAK                              |
| Home / Delete     | CLEAR                              |
| Backspace         | seta para a esquerda               |
| F2 / F3           | salva / carrega o estado (snapshot)|
| F8                | alterna as cores de artefato       |
| F9                | rebobina a fita                    |
| F10               | turbo (sem limite de velocidade)   |
| F11               | tela cheia                         |
| F12 / Shift+F12   | reset / reset a frio               |

Os símbolos seguem o layout US do PC e são traduzidos para o layout do CoCo
(por exemplo, `"` no PC vira SHIFT+2 no CoCo e `=` vira SHIFT+`-`). Um
controle (gamepad) vira o joystick direito, e um segundo controle, o esquerdo.
Com `--mouse`, o mouse faz o papel do joystick direito.

## Snapshots (salvar e carregar estado)

Um snapshot guarda o estado completo da máquina: a RAM inteira, os
registradores da CPU, PIAs, SAM, VDG, o controlador de disco e a interface
de fita. Ao carregar, a execução continua exatamente do ponto salvo. Só a
RAM não bastaria, porque sem os registradores e os chips o programa não
retoma.

As ROMs e as mídias (arquivos de disco e fita) **não** entram no snapshot.
Antes de carregar, o frontend precisa ter carregado as mesmas ROMs; um hash
gravado no arquivo confere isso e recusa snapshots de outra configuração
ou de outra versão do jogo. O formato é binário e portátil
(little-endian, campo a campo, com cerca de 72 KB numa máquina de 64K), então
um arquivo salvo no Windows abre no Linux.

```c
size_t n = coco_snapshot_size(m);
uint8_t *buf = malloc(n);
coco_snapshot_save(m, buf, n);            /* grave buf onde quiser */
...
int r = coco_snapshot_load(m, buf, n);    /* COCO_SNAPSHOT_OK ou erro */
```

O núcleo só trabalha com buffers. Quem decide onde gravar é o frontend: o
emulador SDL usa `--snapshot ARQ` (padrão `coco.snap`), o jogo embarcado usa
a pasta de preferências do usuário (no Windows, `%APPDATA%\Cepecdbr\<jogo>\`)
e o headless tem `--save-state` e `--load-state`.

## Jogos embarcados (sem ROMs)

O emulador reproduz o hardware, e o BASIC é só um programa em ROM. Um jogo
que inicializa o hardware por conta própria roda sem nenhuma ROM da Tandy,
e o binário dele pode ir dentro do executável.

### Como funciona

- O jogo fica no espaço de cartucho (`$C000-$FEFF`, até 16.128 bytes) e começa em `$C000`.
- `coco_load_boot_stub(m, 0xC000)` instala no lugar do BASIC uma "ROM" que
  só tem os vetores: o RESET salta para `$C000`, e IRQ/FIRQ/NMI/SWI apontam
  para a RAM em `$0100-$010F` (IRQ em `$010C`, FIRQ em `$010F`), os mesmos
  endereços que o BASIC usa.
- O jogo grava `JMP rotina` nesses endereços antes de liberar as interrupções.
  Assim, **o mesmo binário funciona como cartucho num CoCo real**: lá, o
  BASIC dá a partida e salta para `$C000`.

### O jogo-exemplo

`examples/game/game.asm` (cerca de 300 linhas) é um jogo de coletar alvos
que mostra tudo o que normalmente viria do BASIC:

- inicialização do SAM e das PIAs;
- interrupção de quadro (60 Hz) com vetor em RAM;
- varredura do teclado;
- texto e semigráficos escritos direto na tela;
- som pelo DAC.

```sh
make game                                # gera build/coleta (o jogo-exemplo)
./build/coleta                           # roda sem nenhum arquivo de ROM
```

No jogo, F2/F3 salvam e carregam, F11 alterna a tela cheia e F12 reinicia.
Com `--dev`, as teclas F8-F10 de desenvolvimento também ficam ativas.

### Usando o seu jogo

```sh
# fonte no formato do lwasm, montado pelo assembler incluído:
make game GAME_ASM=meujogo.asm GAME_TITLE="Meu Jogo" GAME_NAME=meujogo

# ou um binário já montado (ex.: lwasm --raw -o meujogo.bin meujogo.asm):
make game GAME_BIN=meujogo.bin GAME_TITLE="Meu Jogo" GAME_NAME=meujogo
```

O resultado é `build/meujogo` (ou `meujogo.exe` no Windows): um único
executável (mais a `SDL2.dll` no Windows). Durante o desenvolvimento, dá
para testar sem recompilar o frontend:

```sh
./build/coco-sdl --no-rom --cart meujogo.bin
```

No CMake, as mesmas opções existem como variáveis de cache (`-DGAME_BIN=...`,
`-DGAME_TITLE=...`, `-DGAME_NAME=...`).

O `tools/asm6809.py` é um assembler mínimo, feito para o exemplo montar sem
dependências externas. Para projetos maiores, use o
[LWTOOLS](http://www.lwtools.ca/) (`lwasm`) e passe o binário em `GAME_BIN`. Em C,
o compilador CMOC também gera código 6809, mas a biblioteca padrão dele usa
rotinas do BASIC (o `printf`, por exemplo): sem ROM, é preciso trocar essas
rotinas pelas suas.

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

## coco4k: versão em assembly de 4 KB

O diretório `tiny/` tem um experimento separado: o emulador reescrito em
assembly x86 de 32 bits, gerando um executável Windows de **3892 bytes**
(CPU 6809 completa, texto, semigráficos e todos os modos gráficos, teclado;
sem som, fita e disco). Ele é validado contra este emulador em C por testes
diferenciais. Veja [`tiny/PLANO.md`](tiny/PLANO.md).

## Licença

Distribuído sob a [licença MIT](LICENSE). Você pode usar, modificar e
redistribuir o código, inclusive embarcado em jogos comerciais, desde que
mantenha o aviso de copyright e a licença junto com o software. No jogo
embarcado, isso pode ir num arquivo de texto ao lado do executável ou numa
tela de créditos.

A licença cobre apenas o código deste repositório:

- **ROMs da Tandy/Microsoft**: não fazem parte do projeto nem são cobertas por esta licença.
- **SDL2**: usa a licença zlib, compatível com a MIT; ao distribuir a `SDL2.dll`, nada mais é exigido.
- **Seus jogos**: o binário embarcado é seu e pode ter a licença que você quiser.
