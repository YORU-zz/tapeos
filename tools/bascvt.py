#!/usr/bin/env python3
"""bascvt.py -- TinyBASIC <-> Small-C bidirectional converter.

Converts between the TinyBASIC language (TBC/TBI, see
tinybasic.md) and the Small-C subset (SCC/SCI, see
smallc-tos.md) over their COMMON SUBSET:

  types int/char (void as a function return only), global
  scalars and 1-D constant-size arrays, local scalars,
  functions with typed parameters, assignment, if/else,
  while, repeat-until / do-while, for loops, break, return,
  the shared operator set, array indexing, function calls,
  and the shared external library (print/printf/puts,
  input, locate, cls, inkey, stick, vsync, peek, poke,
  putc/putchar).

Anything outside the common subset is NEVER silently
dropped: it is replaced in the output by a comment marker
and listed in the conversion report, and the tool exits
with status 1.  Constructs that are converted by expansion
(compound assignment, ++/-- in statement position, C
do-while <-> repeat-until, K&R definitions) are logged in
the report as expansions and do not affect the exit code.

Usage:
  bascvt.py to-c   INPUT.tb OUTPUT.c
  bascvt.py to-tb  INPUT.c  OUTPUT.tb

The report is written to OUTPUT.report.txt and echoed to
stdout.  Exit code: 0 = clean (expansions allowed),
1 = unconvertible constructs present, 2 = parse error.
"""
import sys

# ----------------------------------------------------------------
# Lexer (two dialects sharing one scanner)
# ----------------------------------------------------------------

TB_KEYWORDS = {
    "dim", "int", "char", "void", "func", "endfunc",
    "for", "to", "step", "next", "while", "endwhile",
    "repeat", "until", "if", "then", "else", "endif",
    "break", "return",
}
C_KEYWORDS = {
    "int", "char", "void", "if", "else", "while", "do",
    "for", "break", "return",
}

OPS2 = ("<<", ">>", "<=", ">=", "==", "!=", "&&", "||",
        "++", "--", "+=", "-=", "*=", "/=", "%=")
OPS1 = set("+-*/%<>=!~&|^()[]{},;?:#")


class Tok:
    __slots__ = ("kind", "val", "line")

    def __init__(self, kind, val, line):
        self.kind, self.val, self.line = kind, val, line

    def __repr__(self):
        return f"<{self.kind} {self.val!r} @{self.line}>"


class ParseError(Exception):
    def __init__(self, line, msg):
        super().__init__(f"line {line}: {msg}")
        self.line = line


def lex(src, dialect):
    toks = []
    i, line, n = 0, 1, len(src)
    while i < n:
        c = src[i]
        if c == "\n":
            line += 1
            i += 1
            continue
        if c in " \t\r":
            i += 1
            continue
        if src.startswith("/*", i):
            j = src.find("*/", i + 2)
            if j < 0:
                raise ParseError(line, "unterminated comment")
            line += src.count("\n", i, j)
            i = j + 2
            continue
        if src.startswith("//", i):
            j = src.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == '"':
            j = i + 1
            out = []
            while j < n and src[j] != '"':
                if src[j] == "\\" and dialect == "c" and j + 1 < n:
                    esc = {"n": "\n", "t": "\t", "r": "\r",
                           "0": "\0", "\\": "\\", '"': '"',
                           "'": "'"}
                    out.append(esc.get(src[j + 1], src[j + 1]))
                    j += 2
                else:
                    out.append(src[j])
                    j += 1
            if j >= n:
                raise ParseError(line, "unterminated string")
            toks.append(Tok("str", "".join(out), line))
            i = j + 1
            continue
        if c == "'" and dialect == "c":
            j = i + 1
            if j < n and src[j] == "\\":
                esc = {"n": "\n", "t": "\t", "r": "\r",
                       "0": "\0", "\\": "\\", '"': '"', "'": "'"}
                val = esc.get(src[j + 1], src[j + 1])
                j += 2
            elif j < n:
                val = src[j]
                j += 1
            else:
                raise ParseError(line, "bad char literal")
            if j >= n or src[j] != "'":
                raise ParseError(line, "unterminated char literal")
            toks.append(Tok("num", ord(val), line))
            i = j + 1
            continue
        if c.isdigit():
            j = i
            if src.startswith(("0x", "0X"), i):
                j = i + 2
                while j < n and src[j] in "0123456789abcdefABCDEF":
                    j += 1
                toks.append(Tok("num", int(src[i:j], 16), line))
            else:
                while j < n and src[j].isdigit():
                    j += 1
                toks.append(Tok("num", int(src[i:j]), line))
            i = j
            continue
        if c.isalpha() or c == "_":
            j = i
            while j < n and (src[j].isalnum() or src[j] == "_"):
                j += 1
            word = src[i:j]
            kw = C_KEYWORDS if dialect == "c" else TB_KEYWORDS
            low = word.lower()
            if dialect == "tb" and low in kw:
                toks.append(Tok("kw", low, line))
            elif dialect == "c" and word in kw:
                toks.append(Tok("kw", word, line))
            else:
                toks.append(Tok("id", word, line))
            i = j
            continue
        two = src[i:i + 2]
        if two in OPS2:
            toks.append(Tok("op", two, line))
            i += 2
            continue
        if c in OPS1:
            toks.append(Tok("op", c, line))
            i += 1
            continue
        raise ParseError(line, f"unexpected character {c!r}")
    toks.append(Tok("eof", "", line))
    return toks


# ----------------------------------------------------------------
# AST nodes are tuples; node[0] is the tag.  ("bad", line, what)
# marks an unconvertible construct.
# ----------------------------------------------------------------

class Parser:
    def __init__(self, toks, report):
        self.toks = toks
        self.pos = 0
        self.report = report      # list of (kind, line, text)

    def peek(self):
        return self.toks[self.pos]

    def next(self):
        t = self.toks[self.pos]
        self.pos += 1
        return t

    def at_op(self, op):
        t = self.peek()
        return t.kind == "op" and t.val == op

    def at_kw(self, kw):
        t = self.peek()
        return t.kind == "kw" and t.val == kw

    def eat_op(self, op):
        if self.at_op(op):
            self.next()
            return True
        return False

    def expect_op(self, op):
        t = self.next()
        if t.kind != "op" or t.val != op:
            raise ParseError(t.line, f"expected {op!r}, got {t.val!r}")

    def expect_kw(self, kw):
        t = self.next()
        if t.kind != "kw" or t.val != kw:
            raise ParseError(t.line, f"expected {kw}, got {t.val!r}")

    def expect_id(self):
        t = self.next()
        if t.kind != "id":
            raise ParseError(t.line, f"expected a name, got {t.val!r}")
        return t

    def note(self, kind, line, text):
        self.report.append((kind, line, text))


# Shared expression grammar (both dialects use the same
# operator set and precedence; dialect quirks are handled
# in the primary/postfix parsers of each subclass).
BIN_LEVELS = [
    ("||",),
    ("&&",),
    ("|",),
    ("^",),
    ("&",),
    ("==", "!="),
    ("<", "<=", ">", ">="),
    ("<<", ">>"),
    ("+", "-"),
    ("*", "/", "%"),
]


class ExprParser(Parser):
    def parse_expr(self):
        return self.parse_assign()

    def parse_assign(self):
        lhs = self.parse_ternary()
        t = self.peek()
        if t.kind == "op" and t.val in ("=", "+=", "-=", "*=", "/=", "%="):
            self.next()
            rhs = self.parse_assign()
            if t.val != "=":
                if lhs[0] not in ("name", "index"):
                    self.note("unconvertible", t.line,
                              f"compound assignment {t.val} on a "
                              "non-lvalue")
                    return ("bad", t.line,
                            f"compound assignment {t.val}")
                self.note("expanded", t.line,
                          f"{t.val} expanded to a plain assignment")
                rhs = ("bin", t.val[0], lhs, rhs)
            return ("assign", lhs, rhs, t.line)
        return lhs

    def parse_ternary(self):
        e = self.parse_bin(0)
        t = self.peek()
        if t.kind == "op" and t.val == "?":
            self.next()
            a = self.parse_expr()
            self.expect_op(":")
            b = self.parse_ternary()
            return ("tern", e, a, b, t.line)
        return e

    def parse_bin(self, lvl):
        if lvl >= len(BIN_LEVELS):
            return self.parse_unary()
        e = self.parse_bin(lvl + 1)
        while True:
            t = self.peek()
            if t.kind == "op" and t.val in BIN_LEVELS[lvl]:
                self.next()
                r = self.parse_bin(lvl + 1)
                e = ("bin", t.val, e, r, t.line)
            else:
                return e

    def parse_unary(self):
        t = self.peek()
        if t.kind == "op" and t.val in ("-", "!", "~", "+"):
            self.next()
            return ("un", t.val, self.parse_unary(), t.line)
        if t.kind == "op" and t.val in ("++", "--"):
            self.next()
            e = self.parse_unary()
            return ("preinc", e, t.val, t.line)
        return self.parse_postfix()

    def parse_postfix(self):
        raise NotImplementedError


# ----------------------------------------------------------------
# TinyBASIC parser
# ----------------------------------------------------------------

class TBParser(ExprParser):
    def parse_postfix(self):
        t = self.next()
        if t.kind == "num":
            e = ("num", t.val, t.line)
        elif t.kind == "str":
            e = ("str", t.val, t.line)
        elif t.kind == "id":
            e = ("name", t.val, t.line)
        elif t.kind == "op" and t.val == "(":
            e = self.parse_expr()
            self.expect_op(")")
        else:
            raise ParseError(t.line, f"unexpected {t.val!r}")
        while True:
            if self.at_op("("):
                # call or array index -- same syntax in TB
                self.next()
                args = []
                if not self.at_op(")"):
                    args.append(self.parse_expr())
                    while self.eat_op(","):
                        args.append(self.parse_expr())
                self.expect_op(")")
                if e[0] == "name":
                    e = ("callidx", e[1], args, e[2])
                else:
                    raise ParseError(t.line, "bad call/index target")
            else:
                return e

    def parse_program(self):
        items = []
        while self.peek().kind != "eof":
            if self.at_kw("func"):
                items.append(self.parse_func())
            else:
                items.append(self.parse_global())
        return items

    def parse_type(self):
        t = self.next()
        if t.kind != "kw" or t.val not in ("int", "char", "void"):
            raise ParseError(t.line, "expected a type")
        return t.val

    def parse_global(self):
        line = self.peek().line
        isdim = False
        if self.at_kw("dim"):
            self.next()
            isdim = True
        ty = self.parse_type()
        name = self.expect_id().val
        size = None
        if self.at_op("("):
            self.next()
            t = self.next()
            if t.kind != "num":
                self.note("unconvertible", line,
                          "array size is not a constant")
                size = ("bad", line, "non-constant array size")
            else:
                size = t.val
            self.expect_op(")")
        elif isdim:
            raise ParseError(line, "dim without a size")
        return ("global", ty, name, size, line)

    def parse_func(self):
        line = self.peek().line
        self.expect_kw("func")
        ty = self.parse_type()
        name = self.expect_id().val
        self.expect_op("(")
        params = []
        if not self.at_op(")"):
            params.append(self.parse_param())
            while self.eat_op(","):
                params.append(self.parse_param())
        self.expect_op(")")
        body = self.parse_block(("endfunc",))
        self.expect_kw("endfunc")
        return ("func", ty, name, params, body, line)

    def parse_param(self):
        ty = self.parse_type()
        return (ty, self.expect_id().val)

    def parse_block(self, terminators):
        stmts = []
        while True:
            t = self.peek()
            if t.kind == "eof":
                raise ParseError(t.line, "unterminated block")
            if t.kind == "kw" and t.val in terminators:
                return stmts
            stmts.append(self.parse_stmt())

    def parse_stmt(self):
        t = self.peek()
        line = t.line
        if t.kind == "kw":
            if t.val in ("int", "char"):
                decls = []
                ty = t.val
                self.next()
                while True:
                    nm = self.expect_id().val
                    if self.at_op("("):
                        self.next()
                        self.next()  # size token
                        self.expect_op(")")
                        decls.append(("bad", line,
                                      f"local array {nm}"))
                    else:
                        decls.append((ty, nm))
                    if not self.eat_op(","):
                        break
                return ("decl", decls, line)
            if t.val == "if":
                self.next()
                cond = self.parse_expr()
                self.expect_kw("then")
                then = self.parse_block(("else", "endif"))
                els = []
                if self.at_kw("else"):
                    self.next()
                    els = self.parse_block(("endif",))
                self.expect_kw("endif")
                return ("if", cond, then, els, line)
            if t.val == "while":
                self.next()
                cond = self.parse_expr()
                body = self.parse_block(("endwhile",))
                self.expect_kw("endwhile")
                return ("while", cond, body, line)
            if t.val == "repeat":
                self.next()
                body = self.parse_block(("until",))
                self.expect_kw("until")
                cond = self.parse_expr()
                return ("dowhile", cond, body, line)
            if t.val == "for":
                self.next()
                var = self.expect_id().val
                self.expect_op("=")
                lo = self.parse_expr()
                self.expect_kw("to")
                hi = self.parse_expr()
                step = None
                if self.at_kw("step"):
                    self.next()
                    step = self.parse_expr()
                body = self.parse_block(("next",))
                self.expect_kw("next")
                return ("for", var, lo, hi, step, body, line)
            if t.val == "break":
                self.next()
                return ("break", line)
            if t.val == "return":
                self.next()
                nt = self.peek()
                if nt.kind == "kw" and nt.val in (
                        "endfunc", "endif", "endwhile", "else",
                        "next", "until"):
                    return ("return", None, line)
                return ("return", self.parse_expr(), line)
            raise ParseError(line, f"unexpected keyword {t.val}")
        e = self.parse_expr()
        if e[0] == "assign":
            return ("expr", e, line)
        if e[0] == "callidx":
            return ("expr", e, line)
        raise ParseError(line, "expected a statement")


# ----------------------------------------------------------------
# Small-C parser
# ----------------------------------------------------------------

class CParser(ExprParser):
    def __init__(self, toks, report):
        super().__init__(toks, report)
        self.globals = set()      # names of global arrays
        self.funcs = set()        # function names

    def parse_postfix(self):
        t = self.next()
        if t.kind == "num":
            e = ("num", t.val, t.line)
        elif t.kind == "str":
            e = ("str", t.val, t.line)
        elif t.kind == "id":
            e = ("name", t.val, t.line)
        elif t.kind == "op" and t.val == "(":
            # cast or parenthesised expression
            save = self.pos
            nt = self.peek()
            if nt.kind == "kw" and nt.val in ("int", "char", "void"):
                self.next()
                stars = 0
                while self.eat_op("*"):
                    stars += 1
                if self.at_op(")"):
                    self.next()
                    operand = self.parse_unary()
                    if stars:
                        return ("bad", t.line, "pointer cast")
                    if nt.val == "char":
                        return ("bad", t.line,
                                "(char) cast inside an "
                                "expression (assign to a char "
                                "variable instead)")
                    # (int) is the identity on char values
                    # (zero-extended) and on ints: drop it.
                    self.note("expanded", t.line,
                              "(int) cast dropped (identity in "
                              "the common subset)")
                    return operand
            self.pos = save
            e = self.parse_expr()
            self.expect_op(")")
        elif t.kind == "op" and t.val == "*":
            operand = self.parse_unary()
            return ("bad", t.line,
                    "dereference * (pointers have no "
                    "TinyBASIC form)")
        elif t.kind == "op" and t.val == "&":
            operand = self.parse_unary()
            return ("bad", t.line,
                    "address-of & (pointers have no "
                    "TinyBASIC form)")
        else:
            raise ParseError(t.line, f"unexpected {t.val!r}")
        while True:
            if self.at_op("("):
                self.next()
                args = []
                if not self.at_op(")"):
                    args.append(self.parse_expr())
                    while self.eat_op(","):
                        args.append(self.parse_expr())
                self.expect_op(")")
                if e[0] != "name":
                    raise ParseError(t.line, "bad call target")
                e = ("call", e[1], args, e[2])
            elif self.at_op("["):
                self.next()
                idx = self.parse_expr()
                self.expect_op("]")
                e = ("index", e, idx, t.line)
            elif self.at_op("++") or self.at_op("--"):
                op = self.next().val
                e = ("postinc", e, op, t.line)
            else:
                return e

    def parse_program(self):
        items = []
        while self.peek().kind != "eof":
            if self.at_op("#"):
                self.next()
                t = self.next()
                if t.kind == "id" and t.val in ("include", "define"):
                    self.note("unconvertible", t.line,
                              f"#{t.val} has no TinyBASIC form "
                              "(expand it before converting)")
                    # skip to the next directive/item roughly:
                    # tokens of the directive are id/str/num on
                    # the same line
                    ln = t.line
                    while (self.peek().kind in ("id", "str", "num")
                           and self.peek().line == ln):
                        self.next()
                else:
                    raise ParseError(t.line, "bad directive")
                continue
            items.append(self.parse_toplevel())
        return items

    def parse_type(self):
        t = self.next()
        if t.kind != "kw" or t.val not in ("int", "char", "void"):
            raise ParseError(t.line, "expected a type")
        return t.val, t.line

    def parse_toplevel(self):
        ty, line = self.parse_type()
        stars = 0
        while self.eat_op("*"):
            stars += 1
        name = self.expect_id().val
        if self.at_op("("):
            self.next()
            params, kr = self.parse_params()
            if self.eat_op(";"):
                return ("proto", ty, name, params, line)
            if kr:
                params = self.parse_kr_decls(params)
                self.note("expanded", line,
                          "K&R definition rewritten in ANSI "
                          "form")
            body = self.parse_cblock()
            self.funcs.add(name)
            if stars:
                return ("bad", line,
                        "function returning a pointer")
            return ("func", ty, name, params, body, line)
        size = None
        if self.at_op("["):
            self.next()
            t = self.next()
            if t.kind != "num":
                self.note("unconvertible", line,
                          "array size is not a constant")
                size = ("bad", line, "non-constant array size")
            else:
                size = t.val
            self.expect_op("]")
            self.globals.add(name)
        if self.eat_op("="):
            self.parse_expr()
            size = ("bad", line,
                    "global initialiser (no TinyBASIC form)")
        self.expect_op(";")
        if stars:
            return ("bad", line, "global pointer variable")
        return ("global", ty, name, size, line)

    def parse_params(self):
        # current token is just past '('
        params = []
        kr = False
        if self.at_op(")"):
            self.next()
            return params, kr
        if self.at_kw("void") and self.toks[self.pos + 1].val == ")":
            self.next()
            self.next()
            return params, kr
        while True:
            t = self.peek()
            if t.kind == "kw" and t.val in ("int", "char", "void"):
                ty, _ = self.parse_type()
                st = 0
                while self.eat_op("*"):
                    st += 1
                nm = self.expect_id().val
                if st:
                    params.append(("bad", t.line,
                                   "pointer parameter"))
                else:
                    params.append((ty, nm))
            elif t.kind == "id":
                # K&R: bare name, type declared later
                params.append(("kr", t.val))
                kr = True
                self.next()
            else:
                raise ParseError(t.line, "bad parameter")
            if not self.eat_op(","):
                break
        self.expect_op(")")
        return params, kr

    def parse_kr_decls(self, params):
        types = {}
        while not self.at_op("{"):
            ty, _ = self.parse_type()
            while True:
                nm = self.expect_id().val
                types[nm] = ty
                if not self.eat_op(","):
                    break
            self.expect_op(";")
        return [(types.get(nm, "int"), nm) for (_, nm) in params]

    def parse_cblock(self):
        self.expect_op("{")
        stmts = []
        while not self.at_op("}"):
            if self.peek().kind == "eof":
                raise ParseError(self.peek().line,
                                 "unterminated block")
            stmts.append(self.parse_cstmt())
        self.next()
        return stmts

    def parse_cstmt(self):
        t = self.peek()
        line = t.line
        if self.at_op("{"):
            return ("block", self.parse_cblock(), line)
        if t.kind == "kw" and t.val in ("int", "char"):
            ty, _ = self.parse_type()
            decls = []
            while True:
                st = 0
                while self.eat_op("*"):
                    st += 1
                nm = self.expect_id().val
                if st:
                    decls.append(("bad", line,
                                  "local pointer variable"))
                elif self.at_op("["):
                    self.next()
                    self.next()
                    self.expect_op("]")
                    decls.append(("bad", line,
                                  "local array (TinyBASIC "
                                  "locals are scalars; make "
                                  "it global)"))
                else:
                    if self.eat_op("="):
                        init = self.parse_expr()
                        decls.append((ty, nm, init))
                    else:
                        decls.append((ty, nm, None))
                if not self.eat_op(","):
                    break
            self.expect_op(";")
            out = [("decl1", d, line) for d in decls]
            return ("block", out, line)
        if t.kind == "kw" and t.val == "if":
            self.next()
            self.expect_op("(")
            cond = self.parse_expr()
            self.expect_op(")")
            then = [self.parse_cstmt()]
            els = []
            if self.at_kw("else"):
                self.next()
                els = [self.parse_cstmt()]
            return ("if", cond, then, els, line)
        if t.kind == "kw" and t.val == "while":
            self.next()
            self.expect_op("(")
            cond = self.parse_expr()
            self.expect_op(")")
            return ("while", cond, [self.parse_cstmt()], line)
        if t.kind == "kw" and t.val == "do":
            self.next()
            body = [self.parse_cstmt()]
            self.expect_kw("while")
            self.expect_op("(")
            cond = self.parse_expr()
            self.expect_op(")")
            self.expect_op(";")
            self.note("expanded", line,
                      "do-while becomes repeat-until with "
                      "the condition inverted")
            return ("dowhile", ("un", "!", cond, line), body, line)
        if t.kind == "kw" and t.val == "for":
            self.next()
            self.expect_op("(")
            init = None
            if not self.at_op(";"):
                init = self.parse_expr()
            self.expect_op(";")
            cond = None
            if not self.at_op(";"):
                cond = self.parse_expr()
            self.expect_op(";")
            incr = None
            if not self.at_op(")"):
                incr = self.parse_expr()
            self.expect_op(")")
            body = [self.parse_cstmt()]
            return ("cfor", init, cond, incr, body, line)
        if t.kind == "kw" and t.val == "break":
            self.next()
            self.expect_op(";")
            return ("break", line)
        if t.kind == "kw" and t.val == "return":
            self.next()
            if self.eat_op(";"):
                return ("return", None, line)
            e = self.parse_expr()
            self.expect_op(";")
            return ("return", e, line)
        if self.eat_op(";"):
            return ("block", [], line)
        e = self.parse_expr()
        self.expect_op(";")
        if e[0] in ("postinc", "preinc") and \
                e[1][0] in ("name", "index"):
            op = "+" if e[2] == "++" else "-"
            self.note("expanded", line,
                      f"statement {e[2]} expanded to a plain "
                      "assignment")
            return ("expr",
                    ("assign", e[1],
                     ("bin", op, e[1], ("num", 1, line), line),
                     line), line)
        return ("expr", e, line)


# ----------------------------------------------------------------
# Resolution: TB callidx -> call / index, using the symbol sets
# ----------------------------------------------------------------

TB_EXTERN = {"print", "input", "locate", "cls", "inkey",
             "stick", "vsync", "peek", "poke", "putc"}
C_EXTERN = {"printf", "puts", "putchar", "getchar", "input",
            "locate", "cls", "inkey", "stick", "vsync",
            "peek", "poke"}


def resolve(items, arrays, funcs, externs, report):
    def walk_e(e):
        if not isinstance(e, tuple):
            return e
        tag = e[0]
        if tag == "callidx":
            name, args, line = e[1], [walk_e(a) for a in e[2]], e[3]
            low = name.lower()
            if low in funcs or low in externs:
                return ("call", name, args, line)
            if low in arrays:
                if len(args) != 1:
                    report.append(("unconvertible", line,
                                   f"array {name} indexed with "
                                   f"{len(args)} subscripts"))
                    return ("bad", line, "array index count")
                return ("index", ("name", name, line), args[0],
                        line)
            report.append(("unconvertible", line,
                           f"unknown name {name} used with ( )"))
            return ("bad", line, f"unknown {name}")
        return tuple(walk_e(x) if isinstance(x, tuple) else x
                     for x in e)

    def walk_s(s):
        return tuple(walk_e(x) if isinstance(x, tuple)
                     else [walk_s(y) for y in x]
                     if isinstance(x, list) else x for x in s)

    return [walk_s(it) for it in items]


# ----------------------------------------------------------------
# Emitters
# ----------------------------------------------------------------

class Emitter:
    def __init__(self, report):
        self.report = report
        self.out = []

    def bad(self, line, what):
        self.report.append(("unconvertible", line, what))
        return f"/* UNCONVERTIBLE: {what} */"


def c_escape(s):
    out = []
    for ch in s:
        if ch == '"':
            out.append('\\"')
        elif ch == "\\":
            out.append("\\\\")
        elif ch == "\n":
            out.append("\\n")
        elif ch == "\t":
            out.append("\\t")
        elif ch == "\r":
            out.append("\\r")
        elif ord(ch) < 32 or ord(ch) > 126:
            out.append("\\%03o" % ord(ch))
        else:
            out.append(ch)
    return "".join(out)


class CEmitter(Emitter):
    def emit_program(self, items):
        for it in items:
            self.emit_item(it)
        return "\n".join(self.out) + "\n"

    def emit_item(self, it):
        tag = it[0]
        if tag == "global":
            _, ty, name, size, line = it
            if isinstance(size, tuple):
                self.out.append(self.bad(line, "global"))
            elif size is None:
                self.out.append(f"{ty} {name};")
            else:
                self.out.append(f"{ty} {name}[{size}];")
        elif tag == "proto":
            _, ty, name, params, line = it
            ps = ", ".join(f"{t} {n}" for t, n in params
                           if isinstance(t, str))
            self.out.append(f"{ty} {self.cname(name)}({ps});")
        elif tag == "func":
            _, ty, name, params, body, line = it
            ps = ", ".join(self.param_str(p) for p in params)
            self.out.append(f"{ty} {self.cname(name)}({ps})")
            self.out.append("{")
            for s in body:
                self.emit_stmt(s, 1)
            self.out.append("}")
            self.out.append("")
        elif tag == "bad":
            self.out.append(self.bad(it[1], it[2]))
        else:
            self.out.append(self.bad(0, f"item {tag}"))

    def param_str(self, p):
        if p[0] == "bad":
            return self.bad(p[1], p[2])
        if isinstance(p[0], str) and p[0] in ("int", "char"):
            return f"{p[0]} {p[1]}"
        return "int /* bad parameter */"

    def cname(self, name):
        return "main" if name.lower() == "main" else name

    def emit_block(self, stmts, ind):
        for s in stmts:
            self.emit_stmt(s, ind)

    def emit_stmt(self, s, ind):
        pad = "    " * ind
        tag = s[0]
        if tag == "decl":
            for d in s[1]:
                if d[0] == "bad":
                    self.out.append(pad + self.bad(d[1], d[2]))
                else:
                    self.out.append(f"{pad}{d[0]} {d[1]};")
        elif tag == "decl1":
            d = s[1]
            if d[0] == "bad":
                self.out.append(pad + self.bad(d[1], d[2]))
            elif d[2] is None:
                self.out.append(f"{pad}{d[0]} {d[1]};")
            else:
                self.out.append(
                    f"{pad}{d[0]} {d[1]} = {self.expr(d[2])};")
        elif tag == "block":
            if s[1]:
                self.out.append(pad + "{")
                self.emit_block(s[1], ind + 1)
                self.out.append(pad + "}")
        elif tag == "expr":
            self.out.append(f"{pad}{self.expr(s[1])};")
        elif tag == "if":
            self.out.append(f"{pad}if ({self.expr(s[1])}) {{")
            self.emit_block(s[2], ind + 1)
            if s[3]:
                self.out.append(pad + "} else {")
                self.emit_block(s[3], ind + 1)
            self.out.append(pad + "}")
        elif tag == "while":
            self.out.append(f"{pad}while ({self.expr(s[1])}) {{")
            self.emit_block(s[2], ind + 1)
            self.out.append(pad + "}")
        elif tag == "dowhile":
            self.out.append(pad + "do {")
            self.emit_block(s[2], ind + 1)
            self.out.append(
                f"{pad}}} while (!({self.expr(s[1])}));")
        elif tag == "for":
            _, var, lo, hi, step, body, line = s
            if step is None:
                cond = f"{var} <= {self.expr(hi)}"
                incr = f"{var} = {var} + 1"
            elif step[0] == "num" and step[1] > 0:
                cond = f"{var} <= {self.expr(hi)}"
                incr = f"{var} = {var} + {step[1]}"
            elif step[0] == "num" and step[1] < 0:
                cond = f"{var} >= {self.expr(hi)}"
                incr = f"{var} = {var} - {-step[1]}"
            elif step[0] == "un" and step[1] == "-" \
                    and step[2][0] == "num":
                cond = f"{var} >= {self.expr(hi)}"
                incr = f"{var} = {var} - {step[2][1]}"
            else:
                se = self.expr(step)
                cond = (f"({se}) >= 0 ? {var} <= {self.expr(hi)}"
                        f" : {var} >= {self.expr(hi)}")
                incr = f"{var} = {var} + ({se})"
                self.report.append(
                    ("expanded", line,
                     "for-step expression: the C condition "
                     "tests the step sign each iteration "
                     "(the TinyBASIC bound is inclusive)"))
            self.out.append(
                f"{pad}for ({var} = {self.expr(lo)}; {cond}; "
                f"{incr}) {{")
            self.emit_block(body, ind + 1)
            self.out.append(pad + "}")
        elif tag == "cfor":
            _, init, cond, incr, body, line = s
            i_s = self.expr(init) if init else ""
            c_s = self.expr(cond) if cond else ""
            n_s = self.expr(incr) if incr else ""
            self.out.append(f"{pad}for ({i_s}; {c_s}; {n_s}) {{")
            self.emit_block(body, ind + 1)
            self.out.append(pad + "}")
        elif tag == "break":
            self.out.append(pad + "break;")
        elif tag == "return":
            if s[1] is None:
                self.out.append(pad + "return;")
            else:
                self.out.append(
                    f"{pad}return {self.expr(s[1])};")
        elif tag == "bad":
            self.out.append(pad + self.bad(s[1], s[2]))
        else:
            self.out.append(pad + self.bad(s[-1], f"stmt {tag}"))

    def expr(self, e):
        tag = e[0]
        if tag == "num":
            v = e[1]
            return str(v - 65536 if v >= 32768 else v) \
                if v > 32767 or v < 0 else str(v)
        if tag == "str":
            return '"' + c_escape(e[1]) + '"'
        if tag == "name":
            return e[1]
        if tag == "bin":
            return (f"({self.expr(e[2])} {e[1]} "
                    f"{self.expr(e[3])})")
        if tag == "un":
            return f"({e[1]}{self.expr(e[2])})"
        if tag == "assign":
            return f"{self.expr(e[1])} = {self.expr(e[2])}"
        if tag == "index":
            return f"{self.expr(e[1])}[{self.expr(e[2])}]"
        if tag == "call":
            return self.call_expr(e)
        if tag == "tern":
            return (f"({self.expr(e[1])} ? {self.expr(e[2])} "
                    f": {self.expr(e[3])})")
        if tag in ("postinc", "preinc"):
            return self.bad(e[3], f"{e[2]} inside an expression")
        if tag == "bad":
            return self.bad(e[1], e[2])
        return self.bad(0, f"expr {tag}")

    def call_expr(self, e):
        _, name, args, line = e
        low = name.lower()
        if low == "print":
            return self.print_call(args, line)
        if low == "putc":
            a = self.expr(args[0]) if args else "0"
            return f"putchar({a})"
        return (f"{self.cname(name)}("
                + ", ".join(self.expr(a) for a in args) + ")")

    def print_call(self, args, line):
        # TinyBASIC print(...) -> printf / puts / putchar
        if not args:
            return "putchar('\\n')"
        if len(args) == 1 and args[0][0] == "str":
            return f'puts("{c_escape(args[0][1])}")'
        fmt = []
        exprs = []
        for a in args:
            if a[0] == "str":
                fmt.append(c_escape(a[1]).replace("%", "%%"))
            else:
                fmt.append("%d")
                exprs.append(self.expr(a))
        f = "".join(fmt) + "\\n"
        if exprs:
            return f'printf("{f}", ' + ", ".join(exprs) + ")"
        return f'printf("{f}")'


class TBEmitter(Emitter):
    def emit_program(self, items):
        for it in items:
            self.emit_item(it)
        return "\n".join(self.out) + "\n"

    def tbname(self, name):
        return "Main" if name == "main" else name

    def emit_item(self, it):
        tag = it[0]
        if tag == "global":
            _, ty, name, size, line = it
            if isinstance(size, tuple):
                self.out.append(self.bad(line, "global"))
            elif size is None:
                self.out.append(f"{ty} {name}")
            else:
                self.out.append(f"dim {ty} {name}({size})")
        elif tag == "proto":
            self.report.append(
                ("expanded", it[4],
                 f"prototype of {it[2]} dropped (TinyBASIC "
                 "needs no prototypes)"))
            self.out.append(
                f"/* prototype {it[2]}: not needed in "
                "TinyBASIC */")
        elif tag == "func":
            _, ty, name, params, body, line = it
            ps = ", ".join(self.param_str(p) for p in params)
            self.out.append(
                f"func {ty} {self.tbname(name)}({ps})")
            decls = []
            self.collect_decls(body, decls)
            for ty2, nm in decls:
                self.out.append(f"    {ty2} {nm}")
            for s in body:
                self.emit_stmt(s, 1)
            self.out.append("endfunc")
            self.out.append("")
        elif tag == "bad":
            self.out.append(self.bad(it[1], it[2]))
        else:
            self.out.append(self.bad(0, f"item {tag}"))

    def param_str(self, p):
        if p[0] == "bad":
            return self.bad(p[1], p[2])
        if isinstance(p[0], str) and p[0] in ("int", "char"):
            return f"{p[0]} {p[1]}"
        return "int /* bad parameter */"

    def collect_decls(self, stmts, acc):
        for s in stmts:
            tag = s[0]
            if tag == "decl":
                for d in s[1]:
                    if d[0] != "bad":
                        acc.append((d[0], d[1]))
            elif tag == "decl1":
                d = s[1]
                if d[0] != "bad":
                    acc.append((d[0], d[1]))
            elif tag == "block":
                self.collect_decls(s[1], acc)
            elif tag == "if":
                self.collect_decls(s[2], acc)
                self.collect_decls(s[3], acc)
            elif tag in ("while", "dowhile"):
                self.collect_decls(s[2], acc)
            elif tag == "for":
                self.collect_decls(s[6], acc)
            elif tag == "cfor":
                self.collect_decls(s[4], acc)

    def emit_block(self, stmts, ind):
        for s in stmts:
            self.emit_stmt(s, ind)

    def emit_stmt(self, s, ind):
        pad = "    " * ind
        tag = s[0]
        if tag == "decl":
            for d in s[1]:
                if d[0] == "bad":
                    self.out.append(pad + self.bad(d[1], d[2]))
            # plain decls were hoisted to the function top
        elif tag == "decl1":
            d = s[1]
            if d[0] == "bad":
                self.out.append(pad + self.bad(d[1], d[2]))
            elif d[2] is not None:
                self.out.append(
                    f"{pad}{d[1]} = {self.expr(d[2])}")
        elif tag == "block":
            self.emit_block(s[1], ind)
        elif tag == "expr":
            e = s[1]
            if e[0] == "call" and e[1].lower() in (
                    "printf", "puts", "putchar"):
                self.out.append(
                    pad + self.io_call(e, statement=True))
            else:
                self.out.append(f"{pad}{self.expr(e)}")
        elif tag == "if":
            self.out.append(
                f"{pad}if {self.expr(s[1])} then")
            self.emit_block(s[2], ind + 1)
            if s[3]:
                self.out.append(pad + "else")
                self.emit_block(s[3], ind + 1)
            self.out.append(pad + "endif")
        elif tag == "while":
            self.out.append(f"{pad}while {self.expr(s[1])}")
            self.emit_block(s[2], ind + 1)
            self.out.append(pad + "endwhile")
        elif tag == "dowhile":
            self.out.append(pad + "repeat")
            self.emit_block(s[2], ind + 1)
            self.out.append(
                f"{pad}until {self.expr(s[1])}")
        elif tag == "for":
            _, var, lo, hi, step, body, line = s
            st = f" step {self.expr(step)}" if step else ""
            self.out.append(
                f"{pad}for {var} = {self.expr(lo)} to "
                f"{self.expr(hi)}{st}")
            self.emit_block(body, ind + 1)
            self.out.append(pad + "next")
        elif tag == "cfor":
            self.emit_cfor(s, ind)
        elif tag == "break":
            self.out.append(pad + "break")
        elif tag == "return":
            if s[1] is None:
                self.out.append(pad + "return")
            else:
                self.out.append(
                    f"{pad}return {self.expr(s[1])}")
        elif tag == "bad":
            self.out.append(pad + self.bad(s[1], s[2]))
        else:
            self.out.append(pad + self.bad(s[-1], f"stmt {tag}"))

    def emit_cfor(self, s, ind):
        pad = "    " * ind
        _, init, cond, incr, body, line = s
        fail = None
        var = lo = hi = step = None
        if init and init[0] == "assign" and \
                init[1][0] == "name":
            var, lo = init[1][1], init[2]
        else:
            fail = "for-loop initialiser is not a simple " \
                   "assignment"
        if not fail and incr:
            if incr[0] == "postinc" and incr[1][0] == "name" \
                    and incr[1][1] == var:
                step = ("num", 1 if incr[2] == "++" else -1,
                        line)
            elif incr[0] == "assign" and \
                    incr[1][0] == "name" and incr[1][1] == var \
                    and incr[2][0] == "bin" and \
                    incr[2][2][0] == "name" and \
                    incr[2][2][1] == var:
                if incr[2][1] == "+":
                    step = incr[2][3]
                elif incr[2][1] == "-":
                    step = ("un", "-", incr[2][3], line)
                else:
                    fail = "for-loop increment is not +/-"
            else:
                fail = "for-loop increment has no TinyBASIC " \
                       "for form"
        if not fail:
            if cond is None:
                fail = "for-loop without a condition"
            elif cond[0] == "tern":
                # the expanded sign-test form written by the
                # TB -> C direction
                c, a, b = cond[1], cond[2], cond[3]
                if c[0] == "bin" and c[1] == ">=" and \
                        c[3][0] == "num" and c[3][1] == 0 and \
                        a[0] == "bin" and a[1] == "<=" and \
                        b[0] == "bin" and b[1] == ">=" and \
                        a[2][0] == "name" and a[2][1] == var and \
                        b[2][0] == "name" and b[2][1] == var:
                    step = c[2]
                    hi = a[3]
                else:
                    fail = "for-loop condition is a ?: form " \
                           "that is not the step-sign pattern"
            elif cond[0] == "bin" and cond[2][0] == "name" \
                    and cond[2][1] == var:
                op, bound = cond[1], cond[3]
                spos = step is not None and step[0] == "num" \
                    and step[1] > 0
                sneg = step is not None and (
                    (step[0] == "num" and step[1] < 0) or
                    (step[0] == "un" and step[1] == "-"))
                if op == "<=" and spos:
                    hi = bound
                elif op == "<" and spos:
                    hi = ("bin", "-", bound, ("num", 1, line),
                          line)
                elif op == ">=" and sneg:
                    hi = bound
                elif op == ">" and sneg:
                    hi = ("bin", "+", bound, ("num", 1, line),
                          line)
                else:
                    fail = "for-loop condition/step do not " \
                           "match the TinyBASIC for form"
            else:
                fail = "for-loop condition is not a simple " \
                       "bound test"
        if fail:
            self.out.append(pad + self.bad(line, fail))
            return
        st = ""
        if step is not None and not (
                step[0] == "num" and step[1] == 1):
            st = f" step {self.expr(step)}"
        self.out.append(
            f"{pad}for {var} = {self.expr(lo)} to "
            f"{self.expr(hi)}{st}")
        self.emit_block(body, ind + 1)
        self.out.append(pad + "next")

    def num(self, v):
        if 0 <= v <= 32767:
            return str(v)
        if 32768 <= v <= 65535:
            return "0x%04X" % v
        return str(v)

    def expr(self, e):
        tag = e[0]
        if tag == "num":
            return self.num(e[1])
        if tag == "str":
            return '"' + e[1].replace('"', '') + '"'
        if tag == "name":
            return e[1]
        if tag == "bin":
            return (f"({self.expr(e[2])} {e[1]} "
                    f"{self.expr(e[3])})")
        if tag == "un":
            return f"({e[1]}{self.expr(e[2])})"
        if tag == "assign":
            return f"{self.expr(e[1])} = {self.expr(e[2])}"
        if tag == "index":
            return f"{self.expr(e[1])}({self.expr(e[2])})"
        if tag == "call":
            return self.call_expr(e)
        if tag == "tern":
            return self.bad(e[4], "?: conditional operator")
        if tag in ("postinc", "preinc"):
            return self.bad(e[3], f"{e[2]} inside an expression")
        if tag == "bad":
            return self.bad(e[1], e[2])
        return self.bad(0, f"expr {tag}")

    def call_expr(self, e):
        _, name, args, line = e
        low = name.lower()
        if low in ("printf", "puts", "putchar", "getchar"):
            return self.io_call(e, statement=False)
        if low == "print":
            inner = ", ".join(self.expr(a) for a in args)
            return f"print({inner})"
        if low == "putc":
            a = self.expr(args[0]) if args else "0"
            return f"putc({a})"
        return (f"{self.tbname(name)}("
                + ", ".join(self.expr(a) for a in args) + ")")

    def io_call(self, e, statement):
        _, name, args, line = e
        low = name.lower()
        if low == "getchar":
            return self.bad(line, "getchar() has no TinyBASIC "
                                  "form (input() reads numbers)")
        if not statement:
            return self.bad(line, f"{low}() used as a value "
                                  "(TinyBASIC print/putc are "
                                  "statements)")
        if low == "putchar":
            if len(args) == 1 and args[0][0] == "num" \
                    and args[0][1] == 10:
                return "print()"
            return f"putc({self.expr(args[0])})"
        if low == "puts":
            if len(args) == 1 and args[0][0] == "str":
                return f'print("{args[0][1]}")'
            return self.bad(line, "puts() of a non-literal")
        # printf: first arg must be a format literal ending
        # in exactly one newline
        if not args or args[0][0] != "str":
            return self.bad(line, "printf() with a non-literal "
                                  "format")
        fmt = args[0][1]
        if not fmt.endswith("\n"):
            return self.bad(line, "printf() without a trailing "
                                  "newline (TinyBASIC print "
                                  "always ends the line)")
        fmt = fmt[:-1]
        parts = []
        lit = []
        ai = 1
        i = 0
        while i < len(fmt):
            ch = fmt[i]
            if ch == "%" and i + 1 < len(fmt):
                nx = fmt[i + 1]
                if nx == "%":
                    lit.append("%")
                    i += 2
                    continue
                if nx == "d":
                    if lit:
                        parts.append(("str", "".join(lit), line))
                        lit = []
                    if ai >= len(args):
                        return self.bad(line, "printf() %d "
                                               "without an "
                                               "argument")
                    parts.append(args[ai])
                    ai += 1
                    i += 2
                    continue
                if nx == "s":
                    if ai >= len(args) or args[ai][0] != "str":
                        return self.bad(line, "printf() %s of "
                                               "a non-literal")
                    lit.append(args[ai][1])
                    ai += 1
                    i += 2
                    continue
                return self.bad(line, f"printf() %{nx} has no "
                                      "TinyBASIC form")
            lit.append(ch)
            i += 1
        if lit:
            parts.append(("str", "".join(lit), line))
        inner = ", ".join(self.expr(a) for a in parts)
        return f"print({inner})"


# ----------------------------------------------------------------
# Driver
# ----------------------------------------------------------------

def convert(direction, src):
    report = []
    if direction == "to-c":
        toks = lex(src, "tb")
        items = TBParser(toks, report).parse_program()
        arrays = {it[2].lower() for it in items
                  if it[0] == "global" and it[3] is not None
                  and not isinstance(it[3], tuple)}
        funcs = {it[2].lower() for it in items
                 if it[0] == "func"}
        items = resolve(items, arrays, funcs,
                        {x.lower() for x in TB_EXTERN}, report)
        out = CEmitter(report).emit_program(items)
    else:
        toks = lex(src, "c")
        items = CParser(toks, report).parse_program()
        out = TBEmitter(report).emit_program(items)
    return out, report


def main():
    if len(sys.argv) != 4 or sys.argv[1] not in ("to-c", "to-tb"):
        print(__doc__)
        return 2
    direction, inp, outp = sys.argv[1], sys.argv[2], sys.argv[3]
    src = open(inp, encoding="utf-8").read()
    try:
        out, report = convert(direction, src)
    except ParseError as ex:
        report = [("unconvertible", ex.line, f"parse error: {ex}")]
        rep = (f"bascvt {direction} {inp} -> {outp}\n"
               f"PARSE ERROR line {ex.line}: {ex}\n")
        open(outp + ".report.txt", "w").write(rep)
        print(rep)
        return 2
    open(outp, "w", encoding="utf-8").write(out)
    nbad = sum(1 for r in report if r[0] == "unconvertible")
    nexp = sum(1 for r in report if r[0] == "expanded")
    lines = [f"bascvt {direction} {inp} -> {outp}",
             f"expanded: {nexp}, unconvertible: {nbad}"]
    for kind, line, text in sorted(report, key=lambda r: r[1]):
        mark = "UNCONVERTIBLE" if kind == "unconvertible" \
            else "expanded"
        lines.append(f"  line {line}: [{mark}] {text}")
    rep = "\n".join(lines) + "\n"
    open(outp + ".report.txt", "w").write(rep)
    print(rep, end="")
    return 1 if nbad else 0


if __name__ == "__main__":
    sys.exit(main())
