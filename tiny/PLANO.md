# coco4k: emulador de CoCo 1/2 em um executável de 4 KB

Este diretório tem uma versão do emulador escrita em **assembly x86 de 32 bits**
que gera um executável Windows de **4084 bytes**, com som. É um experimento separado do
emulador principal em C (que continua sendo a referência): o objetivo aqui é
tamanho mínimo, não fidelidade máxima.

## Resultado

| Item | Situação |
|---|---|
| Tamanho do `coco4k.exe` | **4084 bytes** com som (limite: 4096; o `make` falha se passar) |
| Variantes de diagnóstico (sem som) | cabeçalho PE convencional: 4120 bytes (passa de 4 KB); tabela de importação convencional: 4072 bytes |
| Som | DAC de 6 bits com o som habilitado na PIA, uma amostra por linha (15.700 Hz), tocado com `waveOut*`; **a frequência é idêntica à do emulador em C** nos testes |
| CPU 6809 | todas as instruções documentadas; **zero diferenças** contra a CPU em C em milhões de instruções aleatórias |
| Vídeo | texto, semigráficos SG4 e os 8 modos gráficos (PMODE), CSS 0/1: **tela idêntica pixel a pixel** ao emulador em C |
| Teclado, IRQ de quadro (TIMER) | idênticos ao emulador em C no jogo-exemplo |
| Ciclos de máquina | iguais aos da CPU em C em 98,5% das instruções aleatórias; diferem em `EXG`, `ORCC`/`ANDCC` e `ABX` |
| Wine (Linux) | roda; jogo-exemplo sem ROMs testado com teclado; o som não foi ouvido (o contêiner não tem dispositivo de áudio: `waveOutOpen` falha e o emulador segue mudo) |
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
  O campo `e_lfanew` coincide com o `SectionAlignment`, que vale 4. Nenhum
  diretório de dados é declarado. `make safe` gera a variante convencional
  (4120 bytes, sem som) para diagnóstico.
- **Funções do Windows carregadas por hash, sem tabela de importação:**
  1. a `kernel32` é localizada percorrendo a lista de módulos da PEB
     (`fs:[30h]`), comparando o hash do nome de cada módulo;
  2. cada função é procurada pelo hash do nome na tabela de exportação;
  3. `user32`, `gdi32` e `winmm` são carregadas com `LoadLibraryA`.

  O hash é `h = ror(h,13) + (c | 20h)`, calculado pelo próprio pré-processador
  do NASM a partir dos nomes (`IMPORT Sleep`), então não há arquivo gerado.
  As chamadas viram `call [ebp+deslocamento]`, com 3 bytes em vez de 6.
  Comparado à tabela convencional, economiza cerca de 140 bytes; `make static`
  gera a variante convencional (4072 bytes, sem som) para diagnóstico.
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

- som de 1 bit (PIA1 B) e a fonte de som pelo multiplexador (só o DAC toca);
- fita cassete e controlador de disco (portanto sem Disk BASIC);
- joystick (o comparador sempre responde "alto");
- SG6, cores de artefato e bordas da tela;
- 64K com mapa só de RAM (TY) e página P1: o 6809 vê 32K de RAM;
- interrupção de HS, FIRQ e autostart de cartucho com o BASIC presente;
- ciclos exatos exceto em `EXG`, `ORCC`/`ANDCC` e `ABX` (o quadro continua com 14.934 ciclos).

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
- **`test/audiotest.c`:** cinco ROMs (`test/audioroms.py`) geram ondas
  quadradas no DAC com laços de tempos diferentes (`LEAX`, `DECB`, acesso à
  memória, `JSR`/`RTS`) e uma com o som desligado. O teste mede a frequência
  nas amostras do `coco4k` e no emulador em C: a diferença tem de ficar abaixo
  de 3%. Na primeira versão os laços com modos indexados tocavam 14% e 43% mais
  agudos, porque faltavam ciclos nos modos indexados e nas instruções de 16
  bits. Depois das correções, a diferença é 0,0%. `CICLOS=1 difftest` mostra as
  diferenças de ciclos que sobraram, por opcode.
- **Wine + Xvfb:** o `.exe` abre a janela, roda o jogo e responde ao teclado.
  O rastreamento das chamadas mostra `waveOutOpen`, `waveOutPrepareHeader` e
  `waveOutWrite` sendo chamadas; sem placa de som, falham sem travar.

`make -C tiny test` refaz tudo isso; precisa de Linux ou MSYS2 com
`gcc -m32` e Python 3.

### 5. Próximos passos

1. **Testar no Windows 11 real, ouvir o som e rodar as ROMs do BASIC.** É o risco principal (veja abaixo).
2. **Importar funções por hash** — **feito.** Liberou 140 bytes, e as variantes
   de segurança também passaram a caber em 4 KB (até entrar o som).
3. **Som** — **feito**, com 152 bytes (veja abaixo). As correções de ciclos
   que o teste de áudio exigiu custaram mais 40. Sobram 12 bytes. Dá para
   ganhar ~30 encolhendo o carregador: assumir a kernel32 como 3º módulo em
   vez de comparar nomes, e compartilhar o laço de hash.
4. **Joystick pelo mouse** (comparador com o DAC): cerca de 40 bytes.
5. **Opcional, compressão:** com um empacotador de 4K como o
   [Crinkler](https://github.com/runestubbe/Crinkler) (licença zlib), o código
   pode crescer para ~6-7 KB descomprimidos. Isso permitiria trazer fita ou
   disco, mas exige montar objetos COFF e linkar com as bibliotecas do Windows SDK.

## Riscos conhecidos

- **Carregador do Windows:** o cabeçalho sobreposto e o alinhamento de 4 bytes
  seguem a especificação PE e são usados por executáveis mínimos no Windows
  10/11. Mas só foram testados no Wine, que é mais tolerante. Se o Windows
  recusar, use `make safe` (cabeçalho convencional, sem som) para confirmar
  que o problema é o cabeçalho.
- **Funções por hash:** confirmei no Wine que nenhuma das 16 funções colide com
  outro nome nem é encaminhada para outra DLL. No Windows 10/11, algumas
  funções da kernel32 são encaminhadas (por exemplo, `HeapAlloc` aponta para
  `NTDLL.RtlAllocateHeap`), e o carregador não trata esse caso. **Antes de
  testar no Windows, rode `make -C tiny dllcheck DLLDIR=/c/Windows/SysWOW64`**
  (no MSYS2) ou `python tiny/tools/apihash.py C:\Windows\SysWOW64`. Se
  apontar problema, use `make static` (sem som).
- **Antivírus:** executáveis minúsculos com código automodificável costumam
  disparar falsos positivos (acontece com intros de 4K). Percorrer a PEB e
  procurar funções por hash é exatamente o que shellcode faz, o que aumenta
  esse risco. Pode ser preciso liberar o arquivo no Windows Defender; a
  variante `make static` evita a parte do hash.
- **Som:** só foi verificado numericamente (frequência e amplitude das
  amostras). O buffer circular tem 8 blocos de um quadro (≈133 ms). Se o som
  picotar, aumente `NBUF`; se atrasar muito, diminua. O emulador marca o ritmo
  pelo relógio (60 quadros/s), não pela placa de som: se os dois divergirem ao
  longo de minutos, um bloco pode ser descartado (quando ainda está na fila) e
  ouve-se um estalo.
- **BASIC real:** a CPU é idêntica à do emulador em C, mas o modelo das PIAs e
  do SAM é mais simples. Se o BASIC não iniciar, o próximo passo de depuração é
  comparar os dois emuladores rodando a ROM real, com o mesmo `machtest`.

## Como compilar

Requisito: [NASM](https://www.nasm.us/) 2.14 ou mais novo. No MSYS2:
`pacman -S nasm`.

```sh
make -C tiny            # gera build/tiny/coco4k.exe e confere os 4096 bytes
make -C tiny safe       # diagnóstico: cabeçalho PE convencional, sem som
make -C tiny static     # diagnóstico: tabela de importação convencional, sem som
make -C tiny dllcheck DLLDIR=/c/Windows/SysWOW64   # confere os hashes no Windows
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

## Som

- **Origem:** o DAC de 6 bits fica nos bits 7-2 da PIA1 A (`$FF20`). O som
  toca quando o bit 3 do controle da PIA1 B (`$FF23`, CB2) está ligado. Com ele
  desligado, a amostra é o silêncio (`80h`).
- **Amostragem:** ao fim de cada linha de vídeo, `core_frame` grava um byte;
  262 linhas por quadro × 59,94 quadros/s = 15.700 amostras/s, 8 bits sem sinal,
  mono. São 8 instruções no núcleo, compiladas só com `AUDIO` definido.
- **Saída:** `waveOutOpen` com `WAVE_MAPPER`, sem callback. Há 8 cabeçalhos
  `WAVEHDR` e 8 blocos de 262 bytes na pilha. A cada quadro, o bloco da vez
  é preparado e enviado se a placa já o devolveu (`WHDR_INQUEUE` apagado);
  senão, o quadro fica mudo.

## Orçamento de bytes (4084 no total)

O som custou 152 bytes (núcleo, nomes e hashes da winmm, formato `WAVEFORMATEX`
e as chamadas por quadro). As correções de ciclos custaram 40. A tabela abaixo é
a da versão de 3892 bytes, antes dos dois.

| Parte | Bytes | Antes do hash |
|---|---:|---:|
| Cabeçalhos MZ/PE | 160 | 176 |
| Funções do Windows (hashes 78 + carregador 175) | 253 | 339 (tabela de importação) |
| Dados (nomes, paleta, mapa de teclas) | 172 | 172 |
| CPU 6809 | 1974 | 1974 |
| PIAs e SAM | 195 | 195 |
| Quadro e vídeo (VDG) | 389 | 389 |
| Fonte 6847 compactada | 286 | 286 |
| Reset e programa Windows | 462 | 502 (chamadas de 6 bytes) |
