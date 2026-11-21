/*
 * bin2c.c - Converte um binario (ex.: jogo de cartucho) em fonte C para ser
 * embarcado no executavel do frontend de jogo.
 *
 * Uso: bin2c entrada.bin saida.c "Titulo do Jogo" [identificador]
 *
 * Gera:
 *   const unsigned char game_rom[];       conteudo do binario
 *   const unsigned long game_rom_size;    tamanho em bytes
 *   const char game_title[];              titulo da janela
 *   const char game_id[];                 nome curto (pasta dos snapshots)
 */
#include <ctype.h>
#include <stdio.h>
#include <string.h>

static void put_cstring(FILE *out, const char *s)
{
    fputc('"', out);
    for (; *s; s++) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') fprintf(out, "\\%c", c);
        else if (c < 32 || c > 126) fprintf(out, "\\%03o", c);
        else fputc(c, out);
    }
    fputc('"', out);
}

int main(int argc, char **argv)
{
    FILE *in, *out;
    char id[64];
    int c;
    unsigned long n = 0;
    size_t k = 0;
    const char *src;

    if (argc < 4) {
        fprintf(stderr, "uso: bin2c entrada.bin saida.c \"Titulo\" [identificador]\n");
        return 2;
    }
    in = fopen(argv[1], "rb");
    if (!in) { perror(argv[1]); return 1; }
    out = fopen(argv[2], "w");
    if (!out) { perror(argv[2]); fclose(in); return 1; }

    /* identificador: argumento ou titulo em minusculas, so [a-z0-9_] */
    src = argc > 4 ? argv[4] : argv[3];
    for (; *src && k < sizeof(id) - 1; src++) {
        unsigned char ch = (unsigned char)*src;
        if (isalnum(ch)) id[k++] = (char)tolower(ch);
        else if (k && id[k - 1] != '_') id[k++] = '_';
    }
    while (k && id[k - 1] == '_') k--;
    if (!k) { strcpy(id, "jogo"); k = 4; }
    id[k] = 0;

    fprintf(out, "/* Gerado por tools/bin2c a partir de %s - nao editar. */\n\n", argv[1]);
    fprintf(out, "const char game_title[] = ");
    put_cstring(out, argv[3]);
    fprintf(out, ";\nconst char game_id[] = \"%s\";\n\n", id);
    fprintf(out, "const unsigned char game_rom[] = {");
    while ((c = fgetc(in)) != EOF) {
        fprintf(out, "%s0x%02x,", (n % 12) ? " " : "\n    ", c);
        n++;
    }
    if (n == 0) fprintf(out, "0");
    fprintf(out, "\n};\n\nconst unsigned long game_rom_size = %luUL;\n", n);
    fclose(in);
    if (fclose(out) != 0) { perror(argv[2]); return 1; }
    fprintf(stderr, "%s: %lu bytes, titulo \"%s\", id \"%s\"\n", argv[2], n, argv[3], id);
    return 0;
}
