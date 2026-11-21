#!/usr/bin/env python3
"""
asm6809.py - Assembler 6809 minimo (duas ou mais passagens).

Feito para montar o jogo-exemplo sem dependencias externas.  A sintaxe
segue o lwasm (LWTOOLS), entao o mesmo fonte pode ser montado com:

    lwasm --raw -o jogo.bin jogo.asm

Uso: asm6809.py entrada.asm saida.bin [listagem.lst]

Suporta:
  - todas as instrucoes documentadas do 6809
  - modos: inerente, imediato (#), direto (<), estendido (> ou padrao),
    indexado (,R  n,R  ,R+  ,R++  ,-R  ,--R  A,R  B,R  D,R  n,PCR  [..])
  - diretivas: ORG, EQU, FCB, FDB, FCC, RMB, FILL, END
  - expressoes: + - * / & | << >>, parenteses, $hex, %bin, 'c', * (PC)
  - comentarios com ';' ou linha iniciada por '*'
  - rotulos na coluna 0 (':' final opcional)
"""
import re
import sys

# --------------------------------------------------------------------------
# Tabelas de opcodes
# --------------------------------------------------------------------------

# Grupo "acumulador": opcode da coluna imediata; dir=+0x10 idx=+0x20 ext=+0x30
ALU = {
    'SUBA': 0x80, 'CMPA': 0x81, 'SBCA': 0x82, 'ANDA': 0x84, 'BITA': 0x85,
    'LDA': 0x86, 'EORA': 0x88, 'ADCA': 0x89, 'ORA': 0x8A, 'ADDA': 0x8B,
    'SUBB': 0xC0, 'CMPB': 0xC1, 'SBCB': 0xC2, 'ANDB': 0xC4, 'BITB': 0xC5,
    'LDB': 0xC6, 'EORB': 0xC8, 'ADCB': 0xC9, 'ORB': 0xCA, 'ADDB': 0xCB,
}
# 16 bits com imediato de 2 bytes: (opcode imediato, prefixo)
ALU16 = {
    'SUBD': (0x83, None), 'ADDD': (0xC3, None), 'CMPX': (0x8C, None),
    'LDD': (0xCC, None), 'LDX': (0x8E, None), 'LDU': (0xCE, None),
    'CMPD': (0x83, 0x10), 'CMPY': (0x8C, 0x10), 'LDY': (0x8E, 0x10),
    'LDS': (0xCE, 0x10), 'CMPU': (0x83, 0x11), 'CMPS': (0x8C, 0x11),
}
# Gravacoes / JSR: opcode da coluna direta; idx=+0x10 ext=+0x20
STORE = {
    'STA': (0x97, None), 'STB': (0xD7, None), 'STD': (0xDD, None),
    'STX': (0x9F, None), 'STU': (0xDF, None), 'JSR': (0x9D, None),
    'STY': (0x9F, 0x10), 'STS': (0xDF, 0x10),
}
# Leitura-modificacao-escrita: dir=op idx=op+0x60 ext=op+0x70
# inerente A = op+0x40, B = op+0x50
RMW = {
    'NEG': 0x00, 'COM': 0x03, 'LSR': 0x04, 'ROR': 0x06, 'ASR': 0x07,
    'ASL': 0x08, 'LSL': 0x08, 'ROL': 0x09, 'DEC': 0x0A, 'INC': 0x0C,
    'TST': 0x0D, 'JMP': 0x0E, 'CLR': 0x0F,
}
INHERENT = {
    'NOP': [0x12], 'SYNC': [0x13], 'DAA': [0x19], 'SEX': [0x1D],
    'RTS': [0x39], 'ABX': [0x3A], 'RTI': [0x3B], 'MUL': [0x3D],
    'SWI': [0x3F], 'SWI1': [0x3F], 'SWI2': [0x10, 0x3F], 'SWI3': [0x11, 0x3F],
}
IMM8_ONLY = {'ORCC': 0x1A, 'ANDCC': 0x1C, 'CWAI': 0x3C}
LEA = {'LEAX': 0x30, 'LEAY': 0x31, 'LEAS': 0x32, 'LEAU': 0x33}
STACK = {'PSHS': 0x34, 'PULS': 0x35, 'PSHU': 0x36, 'PULU': 0x37}
XFER = {'EXG': 0x1E, 'TFR': 0x1F}
BRANCH = {
    'BRA': 0x20, 'BRN': 0x21, 'BHI': 0x22, 'BLS': 0x23, 'BCC': 0x24,
    'BHS': 0x24, 'BCS': 0x25, 'BLO': 0x25, 'BNE': 0x26, 'BEQ': 0x27,
    'BVC': 0x28, 'BVS': 0x29, 'BPL': 0x2A, 'BMI': 0x2B, 'BGE': 0x2C,
    'BLT': 0x2D, 'BGT': 0x2E, 'BLE': 0x2F, 'BSR': 0x8D,
}

REG_CODES = {'D': 0, 'X': 1, 'Y': 2, 'U': 3, 'S': 4, 'PC': 5,
             'A': 8, 'B': 9, 'CC': 10, 'DP': 11}
IDX_REGS = {'X': 0x00, 'Y': 0x20, 'U': 0x40, 'S': 0x60}
PSH_BITS = {'CC': 0x01, 'A': 0x02, 'B': 0x04, 'D': 0x06, 'DP': 0x08,
            'X': 0x10, 'Y': 0x20, 'U': 0x40, 'S': 0x40, 'PC': 0x80}


class AsmError(Exception):
    pass


# --------------------------------------------------------------------------
# Expressoes
# --------------------------------------------------------------------------

TOKEN_RE = re.compile(r"\s*(\$[0-9A-Fa-f]+|%[01]+|\d+|'.|[A-Za-z_.@][A-Za-z0-9_.@$]*|<<|>>|[-+*/&|()~])")


class Expr:
    """Avaliador recursivo descendente. 'unknown' indica rotulo ainda
    nao definido (primeira passagem)."""

    def __init__(self, text, symbols, pc):
        self.toks = []
        pos = 0
        text = text.strip()
        while pos < len(text):
            mt = TOKEN_RE.match(text, pos)
            if not mt or mt.end() == pos:
                raise AsmError("expressao invalida: %r" % text)
            self.toks.append(mt.group(1))
            pos = mt.end()
        self.i = 0
        self.symbols = symbols
        self.pc = pc
        self.unknown = False

    def peek(self):
        return self.toks[self.i] if self.i < len(self.toks) else None

    def take(self):
        t = self.peek()
        self.i += 1
        return t

    def parse(self):
        v = self.p_or()
        if self.peek() is not None:
            raise AsmError("sobra na expressao: %r" % self.peek())
        return v

    def p_or(self):
        v = self.p_and()
        while self.peek() == '|':
            self.take(); v |= self.p_and()
        return v

    def p_and(self):
        v = self.p_shift()
        while self.peek() == '&':
            self.take(); v &= self.p_shift()
        return v

    def p_shift(self):
        v = self.p_add()
        while self.peek() in ('<<', '>>'):
            op = self.take(); r = self.p_add()
            v = v << r if op == '<<' else v >> r
        return v

    def p_add(self):
        v = self.p_mul()
        while self.peek() in ('+', '-'):
            op = self.take(); r = self.p_mul()
            v = v + r if op == '+' else v - r
        return v

    def p_mul(self):
        v = self.p_unary()
        while self.peek() in ('*', '/'):
            op = self.take(); r = self.p_unary()
            v = v * r if op == '*' else (v // r if r else 0)
        return v

    def p_unary(self):
        t = self.peek()
        if t == '-':
            self.take(); return -self.p_unary()
        if t == '+':
            self.take(); return self.p_unary()
        if t == '~':
            self.take(); return ~self.p_unary()
        return self.p_atom()

    def p_atom(self):
        t = self.take()
        if t is None:
            raise AsmError("expressao incompleta")
        if t == '(':
            v = self.p_or()
            if self.take() != ')':
                raise AsmError("falta ')'")
            return v
        if t == '*':
            return self.pc
        if t.startswith('$'):
            return int(t[1:], 16)
        if t.startswith('%'):
            return int(t[1:], 2)
        if t.startswith("'"):
            return ord(t[1])
        if t.isdigit():
            return int(t)
        key = t.upper()
        if key in self.symbols:
            return self.symbols[key]
        self.unknown = True
        return 0


# --------------------------------------------------------------------------
# Assembler
# --------------------------------------------------------------------------

def split_operands(s):
    """Divide por virgulas fora de aspas/colchetes."""
    out, cur, depth, quote = [], '', 0, None
    for ch in s:
        if quote:
            cur += ch
            if ch == quote:
                quote = None
            continue
        if ch in ('"', "'") and not cur.strip():
            quote = ch
            cur += ch
            continue
        if ch in '[(':
            depth += 1
        elif ch in '])':
            depth -= 1
        if ch == ',' and depth == 0:
            out.append(cur.strip())
            cur = ''
        else:
            cur += ch
    out.append(cur.strip())
    return out


class Assembler:
    def __init__(self):
        self.symbols = {}
        self.changed = False
        self.final = False

    def value(self, text, pc):
        e = Expr(text, self.symbols, pc)
        v = e.parse()
        if e.unknown and self.final:
            raise AsmError("simbolo indefinido em %r" % text)
        return v, e.unknown

    def define(self, name, val):
        key = name.upper()
        if self.symbols.get(key) != val:
            self.changed = True
        self.symbols[key] = val

    # ---- modo indexado ------------------------------------------------
    def indexed(self, operand, pc, insn_len_before):
        """Retorna lista de bytes do postbyte + deslocamento.
        insn_len_before: bytes do opcode (para calcular PCR)."""
        indirect = False
        op = operand.strip()
        if op.startswith('['):
            if not op.endswith(']'):
                raise AsmError("indexado indireto sem ']'")
            indirect = True
            op = op[1:-1].strip()
        ind = 0x10 if indirect else 0

        if ',' not in op:
            # [endereco] estendido indireto
            if not indirect:
                raise AsmError("modo indexado invalido: %r" % operand)
            v, _ = self.value(op, pc)
            return [0x9F, (v >> 8) & 0xFF, v & 0xFF]

        off, reg = op.rsplit(',', 1)
        off = off.strip()
        reg = reg.strip().upper()

        # auto incremento/decremento
        m = re.fullmatch(r'(-{0,2})([XYUS])(\+{0,2})', reg)
        if m and (m.group(1) or m.group(3)):
            if off:
                raise AsmError("deslocamento com auto inc/dec: %r" % operand)
            r = IDX_REGS[m.group(2)]
            mode = {('', '+'): 0x00, ('', '++'): 0x01,
                    ('-', ''): 0x02, ('--', ''): 0x03}.get((m.group(1), m.group(3)))
            if mode is None:
                raise AsmError("modo invalido: %r" % operand)
            if indirect and mode in (0x00, 0x02):
                raise AsmError("indireto nao permitido com +/- simples")
            return [0x80 | r | mode | ind]

        if reg in ('PCR', 'PC'):
            v, unknown = self.value(off or '0', pc)
            # tenta 8 bits: instrucao = opcode + postbyte + 1
            end8 = pc + insn_len_before + 2
            d8 = v - end8
            if not unknown and -128 <= d8 <= 127:
                return [0x8C | ind, d8 & 0xFF]
            end16 = pc + insn_len_before + 3
            d16 = (v - end16) & 0xFFFF
            return [0x8D | ind, d16 >> 8, d16 & 0xFF]

        if reg not in IDX_REGS:
            raise AsmError("registrador de indice invalido: %r" % reg)
        r = IDX_REGS[reg]
        up = off.upper()
        if up == '':
            return [0x84 | r | ind]
        if up == 'A':
            return [0x86 | r | ind]
        if up == 'B':
            return [0x85 | r | ind]
        if up == 'D':
            return [0x8B | r | ind]
        v, unknown = self.value(off, pc)
        if unknown:
            v16 = v & 0xFFFF
            return [0x89 | r | ind, v16 >> 8, v16 & 0xFF]
        if v == 0 and not indirect:
            return [0x84 | r]
        if -16 <= v <= 15 and not indirect:
            return [r | (v & 0x1F)]
        if -128 <= v <= 127:
            return [0x88 | r | ind, v & 0xFF]
        v16 = v & 0xFFFF
        return [0x89 | r | ind, v16 >> 8, v16 & 0xFF]

    # ---- operando generico de memoria ---------------------------------
    def memory(self, operand, pc, prefix_len):
        """Retorna (modo, bytes) com modo em 'dir', 'idx', 'ext'."""
        op = operand.strip()
        if op.startswith('[') or ',' in op:
            return 'idx', self.indexed(op, pc, prefix_len + 1)
        if op.startswith('<'):
            v, _ = self.value(op[1:], pc)
            return 'dir', [v & 0xFF]
        if op.startswith('>'):
            op = op[1:]
        v, _ = self.value(op, pc)
        v &= 0xFFFF
        return 'ext', [v >> 8, v & 0xFF]

    # ---- uma instrucao ------------------------------------------------
    def encode(self, mn, operand, pc):
        pre = lambda p: [p] if p is not None else []

        if mn in INHERENT:
            return list(INHERENT[mn])

        if mn in IMM8_ONLY:
            if not operand.startswith('#'):
                raise AsmError("%s exige operando imediato" % mn)
            v, _ = self.value(operand[1:], pc)
            return [IMM8_ONLY[mn], v & 0xFF]

        if mn in ALU:
            base = ALU[mn]
            if operand.startswith('#'):
                v, _ = self.value(operand[1:], pc)
                return [base, v & 0xFF]
            mode, rest = self.memory(operand, pc, 0)
            return [base + {'dir': 0x10, 'idx': 0x20, 'ext': 0x30}[mode]] + rest

        if mn in ALU16:
            base, p = ALU16[mn]
            if operand.startswith('#'):
                v, _ = self.value(operand[1:], pc)
                v &= 0xFFFF
                return pre(p) + [base, v >> 8, v & 0xFF]
            mode, rest = self.memory(operand, pc, len(pre(p)))
            return pre(p) + [base + {'dir': 0x10, 'idx': 0x20, 'ext': 0x30}[mode]] + rest

        if mn in STORE:
            base, p = STORE[mn]
            mode, rest = self.memory(operand, pc, len(pre(p)))
            return pre(p) + [base + {'dir': 0x00, 'idx': 0x10, 'ext': 0x20}[mode]] + rest

        if mn in RMW:
            base = RMW[mn]
            mode, rest = self.memory(operand, pc, 0)
            return [base + {'dir': 0x00, 'idx': 0x60, 'ext': 0x70}[mode]] + rest

        # NEGA, CLRB, ...
        if mn[:-1] in RMW and mn[-1] in 'AB' and mn[:-1] != 'JMP':
            return [RMW[mn[:-1]] + (0x40 if mn[-1] == 'A' else 0x50)]

        if mn in LEA:
            return [LEA[mn]] + self.indexed(operand, pc, 1)

        if mn in STACK:
            bits = 0
            for r in split_operands(operand):
                r = r.upper()
                if r not in PSH_BITS:
                    raise AsmError("registrador invalido em %s: %s" % (mn, r))
                if (mn in ('PSHS', 'PULS') and r == 'S') or (mn in ('PSHU', 'PULU') and r == 'U'):
                    raise AsmError("%s nao pode empilhar %s" % (mn, r))
                bits |= PSH_BITS[r]
            return [STACK[mn], bits]

        if mn in XFER:
            regs = [r.upper() for r in split_operands(operand)]
            if len(regs) != 2 or any(r not in REG_CODES for r in regs):
                raise AsmError("%s exige dois registradores" % mn)
            return [XFER[mn], (REG_CODES[regs[0]] << 4) | REG_CODES[regs[1]]]

        if mn in BRANCH:
            v, unknown = self.value(operand, pc)
            d = v - (pc + 2)
            if not unknown and not -128 <= d <= 127:
                raise AsmError("desvio fora de alcance (%d); use L%s" % (d, mn))
            return [BRANCH[mn], d & 0xFF]

        if mn in ('LBRA', 'LBSR'):
            v, _ = self.value(operand, pc)
            d = (v - (pc + 3)) & 0xFFFF
            return [0x16 if mn == 'LBRA' else 0x17, d >> 8, d & 0xFF]

        if mn.startswith('L') and mn[1:] in BRANCH and mn != 'LBSR':
            v, _ = self.value(operand, pc)
            d = (v - (pc + 4)) & 0xFFFF
            return [0x10, BRANCH[mn[1:]], d >> 8, d & 0xFF]

        raise AsmError("instrucao desconhecida: %s" % mn)

    # ---- passagem completa ----------------------------------------------
    def parse_line(self, line):
        # remove comentarios (';' fora de aspas)
        out, quote = '', None
        for ch in line:
            if quote:
                out += ch
                if ch == quote:
                    quote = None
                continue
            if ch in ('"', '/') and out.rstrip().upper().endswith('FCC'):
                quote = ch
            if ch == ';':
                break
            out += ch
        line = out.rstrip()
        if not line or line.lstrip().startswith('*') and line.startswith('*'):
            return None, None, ''
        label = None
        if not line[0].isspace():
            parts = line.split(None, 1)
            label = parts[0].rstrip(':')
            line = parts[1] if len(parts) > 1 else ''
        line = line.strip()
        if not line:
            return label, None, ''
        parts = line.split(None, 1)
        mn = parts[0].upper()
        operand = parts[1].strip() if len(parts) > 1 else ''
        # instrucoes inerentes podem ter comentario sem ';' depois
        if mn in INHERENT or (mn[:-1] in RMW and mn[-1] in 'AB' and mn[:-1] != 'JMP'):
            operand = ''
        elif mn not in ('FCC',) and operand:
            operand = operand.split(None, 1)[0] if not operand.startswith(('"', "'")) else operand
        return label, mn, operand

    def run_pass(self, lines, emit):
        self.changed = False
        pc = 0
        origin = None
        out = {}
        listing = []
        for lineno, raw in enumerate(lines, 1):
            try:
                label, mn, operand = self.parse_line(raw)
                if mn == 'EQU':
                    if not label:
                        raise AsmError("EQU sem rotulo")
                    v, _ = self.value(operand, pc)
                    self.define(label, v)
                    listing.append((None, [], raw))
                    continue
                if label:
                    self.define(label, pc)
                if mn is None:
                    listing.append((None, [], raw))
                    continue
                data = []
                if mn == 'ORG':
                    pc, _ = self.value(operand, pc)
                    if origin is None:
                        origin = pc
                elif mn == 'END':
                    listing.append((None, [], raw))
                    break
                elif mn == 'FCB':
                    for o in split_operands(operand):
                        v, _ = self.value(o, pc)
                        data.append(v & 0xFF)
                elif mn == 'FDB':
                    for o in split_operands(operand):
                        v, _ = self.value(o, pc)
                        data += [(v >> 8) & 0xFF, v & 0xFF]
                elif mn == 'FCC':
                    d = operand[0]
                    end = operand.index(d, 1)
                    data = [ord(c) for c in operand[1:end]]
                elif mn == 'RMB':
                    v, _ = self.value(operand, pc)
                    pc += v
                    listing.append((pc - v, [], raw))
                    continue
                elif mn == 'FILL':
                    val, cnt = split_operands(operand)
                    v, _ = self.value(val, pc)
                    n, _ = self.value(cnt, pc)
                    data = [v & 0xFF] * n
                else:
                    data = self.encode(mn, operand, pc)
                if origin is None:
                    origin = pc
                if emit:
                    for i, b in enumerate(data):
                        out[pc + i] = b
                listing.append((pc, data, raw))
                pc += len(data)
            except AsmError as e:
                raise AsmError("linha %d: %s\n    %s" % (lineno, e, raw.rstrip()))
        return origin, out, listing


def assemble(text):
    lines = text.splitlines()
    asm = Assembler()
    for _ in range(10):
        asm.run_pass(lines, emit=False)
        if not asm.changed:
            break
    asm.final = True
    origin, out, listing = asm.run_pass(lines, emit=True)
    if not out:
        return origin or 0, b'', listing, asm.symbols
    lo, hi = min(out), max(out)
    blob = bytearray([0xFF] * (hi - lo + 1))
    for addr, b in out.items():
        blob[addr - lo] = b
    return lo, bytes(blob), listing, asm.symbols


def main(argv):
    if len(argv) < 3:
        sys.stderr.write(__doc__)
        return 2
    try:
        with open(argv[1], encoding='utf-8') as f:
            text = f.read()
        origin, blob, listing, symbols = assemble(text)
    except AsmError as e:
        sys.stderr.write("%s: erro: %s\n" % (argv[1], e))
        return 1
    with open(argv[2], 'wb') as f:
        f.write(blob)
    if len(argv) > 3:
        with open(argv[3], 'w', encoding='utf-8') as f:
            for addr, data, raw in listing:
                a = '%04X' % addr if addr is not None else '    '
                hx = ' '.join('%02X' % b for b in data[:6])
                f.write('%s  %-18s %s\n' % (a, hx, raw.rstrip()))
            f.write('\nSimbolos:\n')
            for k in sorted(symbols):
                f.write('  %-16s $%04X\n' % (k, symbols[k] & 0xFFFF))
    sys.stderr.write("%s: %d bytes em $%04X-$%04X\n" %
                     (argv[2], len(blob), origin, origin + len(blob) - 1))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
