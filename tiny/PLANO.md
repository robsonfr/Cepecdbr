# coco4k: emulador de CoCo 1/2 em um executável de 4 KB

Este diretório tem uma versão do emulador escrita em **assembly x86 de 32 bits**
que gera um executável Windows de **4036 bytes**. É um experimento separado do
emulador principal em C (que continua sendo a referência): o objetivo aqui é
tamanho mínimo, não fidelidade máxima.

## Resultado

| Item | Situação |
|---|---|
| Tamanho do `coco4k.exe` | **4036 bytes** (limite: 4096; o `make` falha se passar) |
| CPU 6809 | todas as instruções documentadas; **zero diferenças** contra a CPU em C em milhões de instruções aleatórias |
| Vídeo | texto, semigráficos SG4 e os 8 modos gráficos (PMODE), CSS 0/1: **tela idêntica pixel a pixel** ao emulador em C |
| Teclado, IRQ de quadro (TIMER) | idênticos ao emulador em C no jogo-exemplo |
| Wine (Linux) | roda; jogo-exemplo sem ROMs testado com teclado |
| Windows real | **ainda não testado** |
| Color/Extended BASIC reais | **ainda não testados** (as ROMs não estão neste ambiente) |

## Plano de ação (e o que foi feito em cada etapa)

### 1. Escolher a plataforma e o formato — feito

- **Windows, PE32 de 32 bits.** Roda no Windows 11 (via WOW64). O código x86 de
  32 bits é mais compacto que o de 64 bits: `inc`/`dec` de 1 byte, `push`
  imediato, `pushad` e endereços de 4 bytes.
- **Sem linker e sem biblioteca C:** o NASM gera o arquivo inteiro
  (`nasm -f bin`), inclusive o cabeçalho PE e a tabela de importação, escritos
  à mão em `coco4k.asm`.
- **Alinhamento baixo** (`SectionAlignment = FileAlignment = 4`): arquivo e
  memória têm o mesmo layout, sem preenchimento até 512 bytes. É um recurso
  previsto na especificação PE.
- **Cabeçalho compacto:** o cabeçalho PE começa no offset 4, sobreposto ao MZ.
  O campo `e_lfanew` coincide com o `SectionAlignment`, que vale 4. Só 2
  diretórios de dados são declarados. Economiza 172 bytes;
  `make safe` gera a variante convencional (4208 bytes) como plano B.
- **Sem memória estática:** os 64 KB do 6809, o estado e a tela (≈130 KB) ficam
  na pilha, que o cabeçalho pede comprometida (4 MB). Não há seção de dados nem
  `VirtualAlloc`.

### 2. Definir o que entra e o que sai — feito

Entra o necessário para rodar o Color BASIC, o Extended BASIC e jogos de
cartucho:

- CPU 6809 completa (páginas 1, 2 e 3, todos os modos indexados, IRQ, SWI1-3, CWAI);
- SAM: endereço da tela e bits de modo;
- PIAs: teclado, modo do VDG, sensor de RAM e IRQ de FS (60 Hz);
- VDG: texto com a fonte original, SG4 e os 8 modos gráficos;
- ROMs carregadas de `bas.rom`, `ext.rom` e `cart.rom`; sem `bas.rom`, o RESET salta para o cartucho (mesmo esquema do `coco_load_boot_stub`).

Ficou de fora, comparado ao emulador em C:

- som (DAC, 1 bit), fita cassete e controlador de disco (portanto sem Disk BASIC);
- joystick (o comparador sempre responde "alto");
- SG6, cores de artefato e bordas da tela;
- 64K com mapa só de RAM (TY) e página P1: o 6809 vê 32K de RAM;
- interrupção de HS, FIRQ e autostart de cartucho com o BASIC presente;
- ciclos aproximados por categoria de instrução (o quadro continua com 14.934 ciclos).

### 3. Técnicas de compactação no núcleo — feito

- **As instruções x86 fazem o trabalho do 6809.** Os flags do x86 (CF, OF, AF,
  SF, ZF) têm a mesma semântica dos do 6809 (C, V, H, N, Z). Cada operação
  (`ADD`, `SBC`, `NEG`, `ROR`...) vira uma instrução x86 de 2 ou 3 bytes,
  gravada num "slot" de código automodificável e executada. Uma rotina única
  (`doop`) copia os flags para o CC.
- **Desvios condicionais:** o CC vira EFLAGS (`popfd`) e o `jcc` x86
  equivalente é escrito no código. `BRA` vira `jmp`, e `BRN` vira um
  `test al, imm8` que "engole" o deslocamento.
- **Decodificação por campos do opcode:** modo de endereçamento, acumulador A/B
  e operação saem dos bits do opcode. Só os grupos irregulares ($10-$1F e
  $30-$3F) usam tabelas de saltos de 1 byte por entrada.
- **Registradores na memória** com o mesmo layout dos códigos do `TFR`/`EXG`
  (D, X, Y, U, S e PC a cada 2 bytes): o acesso é por índice, sem `switch`.
- **Fonte compactada:** 64 caracteres × 7 linhas × 5 colunas = 280 bytes de
  bits lidos com `bt`, gerados de `src/core/font6847.c` (`make font`).
- **Vídeo por pixel:** uma rotina calcula a cor de qualquer pixel em qualquer
  modo, em vez de um laço especializado por modo.
- **Janela com a classe pronta `static`**, sem `RegisterClass` nem procedimento
  de janela. A tela é desenhada com `StretchDIBits` a partir de um DIB de 8 bits.

### 4. Validação — feito

Como não há ROMs da Tandy neste ambiente, a validação compara o `coco4k`
com o emulador em C, que já roda o BASIC real:

- **`test/difftest.c`:** a cada iteração, sorteia registradores e uma instrução
  válida, executa um passo nas duas CPUs e compara registradores, flags e os
  64 KB de memória. Foram 15 milhões de instruções sem diferença. O teste
  achou dois bugs: um no assembly (`CMP`/`BIT`) e **um na CPU em C**:
  `STX ,X++` gravava o X antigo, e o 6809 real (conferido no MAME) grava o já
  incrementado. Os dois foram corrigidos.
- **`test/machtest.c`:** roda a ROM de teste, o jogo-exemplo (com teclas) e 16
  ROMs dos modos gráficos nos dois emuladores e compara a tela pixel a pixel.
- **Wine + Xvfb:** o `.exe` abre a janela, roda o jogo e responde ao teclado.

`make -C tiny test` refaz tudo isso; precisa de Linux ou MSYS2 com
`gcc -m32` e Python 3.

### 5. Próximos passos

1. **Testar no Windows 11 real e com as ROMs do BASIC.** É o risco principal (veja abaixo).
2. **Importar funções por hash.** Hoje a tabela de importação ocupa 339 bytes,
   quase 9% do arquivo. O caminho é localizar o kernel32 pelo PEB, procurar as
   funções por hash na tabela de exportação e usar `LoadLibraryA` para user32 e
   gdi32. É a técnica das intros de 4K e deve liberar ~200 bytes. Com isso:
   - a variante de cabeçalho convencional também cabe em 4 KB;
   - sobra espaço para os itens 3 e 4.
3. **Som:** amostrar o DAC uma vez por linha (≈15,7 kHz) e tocar com `waveOut*`
   (winmm). Estimativa: 150 a 200 bytes.
4. **Joystick pelo mouse** (comparador com o DAC): cerca de 40 bytes.
5. **Opcional, compressão:** com um empacotador de 4K como o
   [Crinkler](https://github.com/runestubbe/Crinkler) (licença zlib), o código
   pode crescer para ~6-7 KB descomprimidos. Isso permitiria trazer fita ou
   disco, mas exige montar objetos COFF e linkar com as bibliotecas do Windows SDK.

## Riscos conhecidos

- **Carregador do Windows:** o cabeçalho sobreposto e o alinhamento de 4 bytes
  seguem a especificação PE e são usados por executáveis mínimos no Windows
  10/11. Mas só foram testados no Wine, que é mais tolerante. Se o Windows
  recusar: primeiro `make safe` (cabeçalho convencional, hoje 4208 bytes);
   depois, o passo 2 acima, para que ele volte a caber em 4 KB.
- **Antivírus:** executáveis minúsculos com código automodificável costumam
  disparar falsos positivos (acontece com intros de 4K). Pode ser preciso
  liberar o arquivo no Windows Defender.
- **BASIC real:** a CPU é idêntica à do emulador em C, mas o modelo das PIAs e
  do SAM é mais simples. Se o BASIC não iniciar, o próximo passo de depuração é
  comparar os dois emuladores rodando a ROM real, com o mesmo `machtest`.

## Como compilar

Requisito: [NASM](https://www.nasm.us/) 2.14 ou mais novo. No MSYS2:
`pacman -S nasm`.

```sh
make -C tiny            # gera build/tiny/coco4k.exe e confere os 4096 bytes
make -C tiny test       # testes diferenciais (Linux ou MSYS2 com gcc -m32)
```

Ou diretamente: `nasm -f bin -o coco4k.exe tiny/coco4k.asm`
(execute a partir do diretório `tiny`, por causa dos `%include`).

## Como usar

Coloque no diretório do executável:

| Arquivo | Conteúdo | Obrigatório? |
|---|---|---|
| `bas.rom` | Color BASIC (8K) | não; sem ele, o RESET vai direto para o cartucho |
| `ext.rom` | Extended Color BASIC (8K) | não |
| `cart.rom` | cartucho de até 16K em `$C000` | não |

Para rodar o jogo-exemplo sem ROMs, copie `build/tiny/game.bin` (gerado pelo
`make -C tiny test`) como `cart.rom`.

Teclado: letras, dígitos, setas, Enter, espaço e Shift funcionam
normalmente. Além disso: Esc = BREAK, Home = CLEAR e Backspace = seta para a
esquerda. Para sair, feche a janela.

## Orçamento de bytes (4036 no total)

| Parte | Bytes |
|---|---:|
| Cabeçalhos MZ/PE | 176 |
| Tabela de importação (12 funções) | 339 |
| Dados (nomes, paleta, mapa de teclas) | 172 |
| CPU 6809 | 1974 |
| PIAs e SAM | 195 |
| Quadro e vídeo (VDG) | 389 |
| Fonte 6847 compactada | 286 |
| Reset e programa Windows | 502 |
