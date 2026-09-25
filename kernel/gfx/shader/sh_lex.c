#include "sh_int.h"

void sh_err(ShComp *c, int tok, const char *msg) {
    int line = 1;
    int col = 1;
    int i;
    int start;
    int at;
    if (!c || c->nerr >= SH_ERR_MAX) {
        if (c) {
            c->nerr++;
        }
        return;
    }
    if (tok >= 0 && tok < c->ntok) {
        line = (int)c->tok[tok].line;
        col = (int)c->tok[tok].col;
    }
    c->nerr++;
    sh_app(c->log, SH_LOG_MAX, &c->logn, c->name[0] ? c->name : "shader");
    sh_app_ch(c->log, SH_LOG_MAX, &c->logn, ':');
    sh_app_i(c->log, SH_LOG_MAX, &c->logn, line);
    sh_app_ch(c->log, SH_LOG_MAX, &c->logn, ':');
    sh_app_i(c->log, SH_LOG_MAX, &c->logn, col);
    sh_app(c->log, SH_LOG_MAX, &c->logn, ":\nerror: ");
    sh_app(c->log, SH_LOG_MAX, &c->logn, msg ? msg : "shader error");
    sh_app_ch(c->log, SH_LOG_MAX, &c->logn, '\n');
    start = 0;
    at = 1;
    for (i = 0; i < c->src_len; ++i) {
        if (at == line) {
            start = i;
            break;
        }
        if (c->src[i] == '\n') {
            ++at;
        }
    }
    if (at == line) {
        for (i = start; i < c->src_len && c->src[i] != '\n' && c->src[i] != '\r'; ++i) {
            sh_app_ch(c->log, SH_LOG_MAX, &c->logn, c->src[i]);
        }
        sh_app_ch(c->log, SH_LOG_MAX, &c->logn, '\n');
        for (i = 1; i < col && c->logn + 2 < SH_LOG_MAX; ++i) {
            sh_app_ch(c->log, SH_LOG_MAX, &c->logn, ' ');
        }
        sh_app(c->log, SH_LOG_MAX, &c->logn, "^\n");
    }
}

static int kw(const char *s, int n, int *kind) {
    static const struct {
        const char *n;
        int k;
    } tab[] = {
        {"in", TK_IN},
        {"out", TK_OUT},
        {"uniform", TK_UNIFORM},
        {"const", TK_CONST},
        {"layout", TK_LAYOUT},
        {"smooth", TK_SMOOTH},
        {"void", TK_VOID},
        {"bool", TK_BOOL},
        {"int", TK_INTKW},
        {"float", TK_FLOATKW},
        {"vec2", TK_VEC2},
        {"vec3", TK_VEC3},
        {"vec4", TK_VEC4},
        {"ivec2", TK_IVEC2},
        {"ivec3", TK_IVEC3},
        {"ivec4", TK_IVEC4},
        {"mat3", TK_MAT3},
        {"mat4", TK_MAT4},
        {"sampler2D", TK_SAMPLER2D},
        {"if", TK_IF},
        {"else", TK_ELSE},
        {"for", TK_FOR},
        {"return", TK_RETURN},
        {"discard", TK_DISCARD},
        {"true", TK_TRUE},
        {"false", TK_FALSE},
    };
    int i;
    for (i = 0; i < (int)(sizeof tab / sizeof tab[0]); ++i) {
        if (sh_eqn(s, n, tab[i].n)) {
            *kind = tab[i].k;
            return 1;
        }
    }
    return 0;
}

static int unsupported_qual(const char *s, int n) {
    static const char *bad[] = {
        "attribute", "varying", "highp", "mediump", "lowp", "inout",
        "centroid", "flat", "noperspective", "invariant", "precise",
        "readonly", "writeonly", "coherent", "volatile", "restrict",
        "shared", "patch", "sample", "inout"};
    int i;
    for (i = 0; i < (int)(sizeof bad / sizeof bad[0]); ++i) {
        if (sh_eqn(s, n, bad[i])) {
            return 1;
        }
    }
    return 0;
}

static int push_tok(ShComp *c, Tok t) {
    if (c->ntok >= SH_TOK_MAX) {
        sh_err(c, c->ntok - 1, "too many tokens");
        return -1;
    }
    c->tok[c->ntok++] = t;
    return 0;
}

static float pow10i(int e) {
    float v = 1.f;
    int n = e < 0 ? -e : e;
    int i;
    for (i = 0; i < n; ++i) {
        v *= 10.f;
    }
    return e < 0 ? 1.f / v : v;
}

int sh_lex(ShComp *c) {
    int i = 0;
    int line = 1;
    int col = 1;
    int line_start = 1;
    if (!c) {
        return -1;
    }
    if (c->src_len <= 0) {
        sh_err(c, -1, "empty shader");
        return -1;
    }
    if (c->src_len >= SH_SRC_MAX) {
        sh_err(c, -1, "shader source exceeds the size limit");
        return -1;
    }
    while (i < c->src_len) {
        char ch = c->src[i];
        Tok t;
        memset(&t, 0, sizeof t);
        t.off = (uint32_t)i;
        t.line = (uint16_t)line;
        t.col = (uint16_t)col;
        if (ch == ' ' || ch == '\t' || ch == '\r') {
            ++i;
            ++col;
            continue;
        }
        if (ch == '\n') {
            ++i;
            ++line;
            col = 1;
            line_start = 1;
            continue;
        }
        if (ch == '#' && line_start) {
            int j = i + 1;
            int dl = line;
            int dc = col;
            char word[32];
            int wn = 0;
            ++col;
            while (j < c->src_len && (c->src[j] == ' ' || c->src[j] == '\t')) {
                ++j;
                ++col;
            }
            while (j < c->src_len && ((c->src[j] >= 'a' && c->src[j] <= 'z') ||
                                      (c->src[j] >= 'A' && c->src[j] <= 'Z'))) {
                if (wn + 1 < (int)sizeof word) {
                    word[wn++] = c->src[j];
                }
                ++j;
                ++col;
            }
            word[wn] = 0;
            if (sh_eq(word, "version")) {
                int ver = 0;
                int any = 0;
                while (j < c->src_len && (c->src[j] == ' ' || c->src[j] == '\t')) {
                    ++j;
                    ++col;
                }
                while (j < c->src_len && c->src[j] >= '0' && c->src[j] <= '9') {
                    any = 1;
                    ver = ver * 10 + (c->src[j] - '0');
                    ++j;
                    ++col;
                }
                if (!any) {
                    t.line = (uint16_t)dl;
                    t.col = (uint16_t)dc;
                    if (push_tok(c, t) != 0) {
                        return -1;
                    }
                    sh_err(c, c->ntok - 1, "expected a version number");
                } else if (ver < 110 || ver > 330) {
                    t.line = (uint16_t)dl;
                    t.col = (uint16_t)dc;
                    t.kind = TK_INT;
                    if (push_tok(c, t) != 0) {
                        return -1;
                    }
                    sh_err(c, c->ntok - 1, "unsupported #version for the ChrisOS GLSL subset");
                } else {
                    c->version = ver;
                }
            } else {
                t.line = (uint16_t)dl;
                t.col = (uint16_t)dc;
                t.kind = TK_BAD;
                if (push_tok(c, t) != 0) {
                    return -1;
                }
                sh_err(c, c->ntok - 1, "unsupported preprocessor directive");
            }
            while (j < c->src_len && c->src[j] != '\n') {
                ++j;
            }
            i = j;
            line_start = 0;
            continue;
        }
        line_start = 0;
        if (ch == '/' && i + 1 < c->src_len && c->src[i + 1] == '/') {
            i += 2;
            col += 2;
            while (i < c->src_len && c->src[i] != '\n') {
                ++i;
                ++col;
            }
            continue;
        }
        if (ch == '/' && i + 1 < c->src_len && c->src[i + 1] == '*') {
            int closed = 0;
            i += 2;
            col += 2;
            while (i < c->src_len) {
                if (c->src[i] == '\n') {
                    ++i;
                    ++line;
                    col = 1;
                    line_start = 1;
                    continue;
                }
                if (c->src[i] == '*' && i + 1 < c->src_len && c->src[i + 1] == '/') {
                    i += 2;
                    col += 2;
                    closed = 1;
                    break;
                }
                ++i;
                ++col;
            }
            if (!closed) {
                t.kind = TK_BAD;
                if (push_tok(c, t) != 0) {
                    return -1;
                }
                sh_err(c, c->ntok - 1, "unterminated block comment");
                break;
            }
            continue;
        }
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || ch == '_') {
            int n = 0;
            int k = TK_IDENT;
            while (i < c->src_len) {
                char d = c->src[i];
                if (!((d >= 'a' && d <= 'z') || (d >= 'A' && d <= 'Z') ||
                      (d >= '0' && d <= '9') || d == '_')) {
                    break;
                }
                ++n;
                ++i;
                ++col;
            }
            t.len = (uint16_t)n;
            t.kind = TK_IDENT;
            if (n >= SH_NAME_MAX) {
                if (push_tok(c, t) != 0) {
                    return -1;
                }
                sh_err(c, c->ntok - 1, "identifier exceeds the name limit");
                continue;
            }
            if (kw(c->src + t.off, n, &k)) {
                t.kind = (uint16_t)k;
            } else if (unsupported_qual(c->src + t.off, n)) {
                t.kind = TK_BAD;
                if (push_tok(c, t) != 0) {
                    return -1;
                }
                sh_err(c, c->ntok - 1, "unsupported qualifier");
                continue;
            }
            if (push_tok(c, t) != 0) {
                return -1;
            }
            continue;
        }
        if ((ch >= '0' && ch <= '9') ||
            (ch == '.' && i + 1 < c->src_len && c->src[i + 1] >= '0' && c->src[i + 1] <= '9')) {
            int start = i;
            int is_float = 0;
            float ip = 0.f;
            float fp = 0.f;
            float base = 1.f;
            int exp = 0;
            int esign = 1;
            int saw_digit = 0;
            while (i < c->src_len && c->src[i] >= '0' && c->src[i] <= '9') {
                saw_digit = 1;
                ip = ip * 10.f + (float)(c->src[i] - '0');
                ++i;
                ++col;
            }
            if (i < c->src_len && c->src[i] == '.') {
                is_float = 1;
                ++i;
                ++col;
                while (i < c->src_len && c->src[i] >= '0' && c->src[i] <= '9') {
                    saw_digit = 1;
                    base *= 0.1f;
                    fp += (float)(c->src[i] - '0') * base;
                    ++i;
                    ++col;
                }
            }
            if (i < c->src_len && (c->src[i] == 'e' || c->src[i] == 'E')) {
                int any = 0;
                is_float = 1;
                ++i;
                ++col;
                if (i < c->src_len && (c->src[i] == '+' || c->src[i] == '-')) {
                    if (c->src[i] == '-') {
                        esign = -1;
                    }
                    ++i;
                    ++col;
                }
                while (i < c->src_len && c->src[i] >= '0' && c->src[i] <= '9') {
                    any = 1;
                    exp = exp * 10 + (c->src[i] - '0');
                    ++i;
                    ++col;
                    if (exp > 38) {
                        break;
                    }
                }
                if (!any) {
                    t.kind = TK_BAD;
                    t.len = (uint16_t)(i - start);
                    if (push_tok(c, t) != 0) {
                        return -1;
                    }
                    sh_err(c, c->ntok - 1, "invalid numeric literal");
                    continue;
                }
            }
            if (!saw_digit) {
                t.kind = TK_BAD;
                if (push_tok(c, t) != 0) {
                    return -1;
                }
                sh_err(c, c->ntok - 1, "invalid numeric literal");
                ++i;
                ++col;
                continue;
            }
            t.len = (uint16_t)(i - start);
            if (is_float) {
                float v = ip + fp;
                if (exp) {
                    v *= pow10i(esign * exp);
                }
                t.kind = TK_FLOAT;
                t.fval = v;
            } else {
                t.kind = TK_INT;
                t.ival = (uint32_t)ip;
                t.fval = ip;
            }
            if (push_tok(c, t) != 0) {
                return -1;
            }
            continue;
        }
        t.len = 1;
        if (ch == '(') {
            t.kind = TK_LPAREN;
        } else if (ch == ')') {
            t.kind = TK_RPAREN;
        } else if (ch == '{') {
            t.kind = TK_LBRACE;
        } else if (ch == '}') {
            t.kind = TK_RBRACE;
        } else if (ch == '[') {
            t.kind = TK_LBRACK;
        } else if (ch == ']') {
            t.kind = TK_RBRACK;
        } else if (ch == ';') {
            t.kind = TK_SEMI;
        } else if (ch == ',') {
            t.kind = TK_COMMA;
        } else if (ch == '.') {
            t.kind = TK_DOT;
        } else if (ch == '+') {
            if (i + 1 < c->src_len && c->src[i + 1] == '+') {
                t.kind = TK_INC;
                t.len = 2;
                ++i;
                ++col;
            } else {
                t.kind = TK_PLUS;
            }
        } else if (ch == '-') {
            if (i + 1 < c->src_len && c->src[i + 1] == '-') {
                t.kind = TK_DEC;
                t.len = 2;
                ++i;
                ++col;
            } else {
                t.kind = TK_MINUS;
            }
        } else if (ch == '*') {
            t.kind = TK_STAR;
        } else if (ch == '/') {
            t.kind = TK_SLASH;
        } else if (ch == '%') {
            t.kind = TK_PERCENT;
        } else if (ch == '=') {
            if (i + 1 < c->src_len && c->src[i + 1] == '=') {
                t.kind = TK_EQ;
                t.len = 2;
                ++i;
                ++col;
            } else {
                t.kind = TK_ASSIGN;
            }
        } else if (ch == '!') {
            if (i + 1 < c->src_len && c->src[i + 1] == '=') {
                t.kind = TK_NE;
                t.len = 2;
                ++i;
                ++col;
            } else {
                t.kind = TK_NOT;
            }
        } else if (ch == '<') {
            if (i + 1 < c->src_len && c->src[i + 1] == '=') {
                t.kind = TK_LE;
                t.len = 2;
                ++i;
                ++col;
            } else {
                t.kind = TK_LT;
            }
        } else if (ch == '>') {
            if (i + 1 < c->src_len && c->src[i + 1] == '=') {
                t.kind = TK_GE;
                t.len = 2;
                ++i;
                ++col;
            } else {
                t.kind = TK_GT;
            }
        } else if (ch == '&' && i + 1 < c->src_len && c->src[i + 1] == '&') {
            t.kind = TK_AND;
            t.len = 2;
            ++i;
            ++col;
        } else if (ch == '|' && i + 1 < c->src_len && c->src[i + 1] == '|') {
            t.kind = TK_OR;
            t.len = 2;
            ++i;
            ++col;
        } else {
            t.kind = TK_BAD;
            if (push_tok(c, t) != 0) {
                return -1;
            }
            sh_err(c, c->ntok - 1, "invalid token");
            ++i;
            ++col;
            continue;
        }
        if (push_tok(c, t) != 0) {
            return -1;
        }
        ++i;
        ++col;
    }
    {
        Tok end;
        memset(&end, 0, sizeof end);
        end.kind = TK_EOF;
        end.line = (uint16_t)line;
        end.col = (uint16_t)col;
        end.off = (uint32_t)c->src_len;
        if (push_tok(c, end) != 0) {
            return -1;
        }
    }
    return c->nerr ? -1 : 0;
}
