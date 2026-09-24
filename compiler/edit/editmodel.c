#include "edit/editmodel.h"

#include <stdint.h>
#include <string.h>

#define EDIT_OPS 128
#define EDIT_CHUNK 64

enum { OP_INSERT = 1, OP_DELETE = 2 };

typedef struct EditOp {
    uint8_t kind;
    uint8_t n;
    int group;
    int pos;
    char text[EDIT_CHUNK];
} EditOp;

typedef struct EditBuf {
    int used;
    int dirty;
    char name[64];
    char data[EDIT_CAP];
    int gap_lo;
    int gap_hi;
    EditOp ops[EDIT_OPS];
    int op_at;
    int op_len;
} EditBuf;

static EditBuf g_bufs[EDIT_BUFS];
static int g_active = -1;
static int g_group = 1;

static int logical_len(const EditBuf *b) {
    return EDIT_CAP - (b->gap_hi - b->gap_lo);
}

static int phys(const EditBuf *b, int i) {
    if (i < b->gap_lo) {
        return i;
    }
    return i + (b->gap_hi - b->gap_lo);
}

static char char_at(const EditBuf *b, int i) {
    return b->data[phys(b, i)];
}

static void move_gap(EditBuf *b, int pos) {
    int len = logical_len(b);
    if (pos < 0) {
        pos = 0;
    }
    if (pos > len) {
        pos = pos > len ? len : pos;
    }
    while (b->gap_lo > pos) {
        b->gap_lo--;
        b->gap_hi--;
        b->data[b->gap_hi] = b->data[b->gap_lo];
    }
    while (b->gap_lo < pos) {
        b->data[b->gap_lo] = b->data[b->gap_hi];
        b->gap_lo++;
        b->gap_hi++;
    }
}

static EditBuf *buf_of(int id) {
    if (id < 0 || id >= EDIT_BUFS || !g_bufs[id].used) {
        return 0;
    }
    return &g_bufs[id];
}

static int raw_insert(EditBuf *b, int pos, const char *text, int n) {
    int i;
    if (n < 0) {
        return 0;
    }
    if (logical_len(b) + n > EDIT_CAP) {
        return 0;
    }
    move_gap(b, pos);
    if (b->gap_hi - b->gap_lo < n) {
        return 0;
    }
    for (i = 0; i < n; ++i) {
        b->data[b->gap_lo++] = text[i];
    }
    return 1;
}

static int raw_delete(EditBuf *b, int pos, int n) {
    if (n < 0 || pos < 0 || pos + n > logical_len(b)) {
        return 0;
    }
    move_gap(b, pos);
    if (b->gap_hi + n > EDIT_CAP) {
        return 0;
    }
    b->gap_hi += n;
    return 1;
}

static void drop_redo(EditBuf *b) {
    b->op_len = b->op_at;
}

static int push_op(EditBuf *b, int kind, int pos, const char *text, int n) {
    EditOp *op;
    int i;
    if (n < 0 || n > EDIT_CHUNK || b->op_at >= EDIT_OPS) {
        return 0;
    }
    op = &b->ops[b->op_at++];
    b->op_len = b->op_at;
    op->kind = (uint8_t)kind;
    op->n = (uint8_t)n;
    op->group = g_group;
    op->pos = pos;
    for (i = 0; i < n; ++i) {
        op->text[i] = text[i];
    }
    return 1;
}

static int apply_back(EditBuf *b, const EditOp *op) {
    if (op->kind == OP_INSERT) {
        return raw_delete(b, op->pos, op->n);
    }
    if (op->kind == OP_DELETE) {
        return raw_insert(b, op->pos, op->text, op->n);
    }
    return 0;
}

static int apply_forward(EditBuf *b, const EditOp *op) {
    if (op->kind == OP_INSERT) {
        return raw_insert(b, op->pos, op->text, op->n);
    }
    if (op->kind == OP_DELETE) {
        return raw_delete(b, op->pos, op->n);
    }
    return 0;
}

void edit_reset(void) {
    memset(g_bufs, 0, sizeof(g_bufs));
    g_active = -1;
    g_group = 1;
}

int edit_open(const char *name) {
    int i;
    int k;
    for (i = 0; i < EDIT_BUFS; ++i) {
        if (!g_bufs[i].used) {
            memset(&g_bufs[i], 0, sizeof(g_bufs[i]));
            g_bufs[i].used = 1;
            g_bufs[i].gap_lo = 0;
            g_bufs[i].gap_hi = EDIT_CAP;
            if (name) {
                for (k = 0; name[k] && k < 63; ++k) {
                    g_bufs[i].name[k] = name[k];
                }
                g_bufs[i].name[k] = 0;
            }
            g_active = i;
            return i;
        }
    }
    return -1;
}

int edit_switch(int id) {
    if (!buf_of(id)) {
        return 0;
    }
    g_active = id;
    return 1;
}

int edit_active(void) {
    return g_active;
}

int edit_close(int id, int force) {
    EditBuf *b = buf_of(id);
    int i;
    if (!b) {
        return -1;
    }
    if (b->dirty && !force) {
        return -2;
    }
    memset(b, 0, sizeof(*b));
    if (g_active == id) {
        g_active = -1;
        for (i = 0; i < EDIT_BUFS; ++i) {
            if (g_bufs[i].used) {
                g_active = i;
                break;
            }
        }
    }
    return 0;
}

int edit_dirty(int id) {
    EditBuf *b = buf_of(id);
    return b ? b->dirty : 0;
}

int edit_mark_clean(int id) {
    EditBuf *b = buf_of(id);
    if (!b) {
        return 0;
    }
    b->dirty = 0;
    return 1;
}

int edit_length(int id) {
    EditBuf *b = buf_of(id);
    return b ? logical_len(b) : -1;
}

int edit_insert(int id, int pos, const char *text) {
    EditBuf *b = buf_of(id);
    int n;
    int off;
    if (!b || !text) {
        return 0;
    }
    n = 0;
    while (text[n]) {
        ++n;
    }
    if (logical_len(b) + n > EDIT_CAP) {
        return 0;
    }
    if (b->op_at + (n + EDIT_CHUNK - 1) / EDIT_CHUNK > EDIT_OPS && n > 0) {
        return 0;
    }
    drop_redo(b);
    ++g_group;
    off = 0;
    while (off < n) {
        int c = n - off;
        if (c > EDIT_CHUNK) {
            c = EDIT_CHUNK;
        }
        if (!push_op(b, OP_INSERT, pos + off, text + off, c)) {
            return 0;
        }
        off += c;
    }
    if (!raw_insert(b, pos, text, n)) {
        return 0;
    }
    b->dirty = 1;
    return 1;
}

int edit_delete(int id, int pos, int n) {
    EditBuf *b = buf_of(id);
    int off;
    char tmp[EDIT_CHUNK];
    if (!b || n < 0 || pos < 0 || pos + n > logical_len(b)) {
        return 0;
    }
    if (n == 0) {
        return 1;
    }
    if (b->op_at + (n + EDIT_CHUNK - 1) / EDIT_CHUNK > EDIT_OPS) {
        return 0;
    }
    drop_redo(b);
    ++g_group;
    off = 0;
    while (off < n) {
        int c = n - off;
        int i;
        if (c > EDIT_CHUNK) {
            c = EDIT_CHUNK;
        }
        for (i = 0; i < c; ++i) {
            tmp[i] = char_at(b, pos + off + i);
        }
        if (!push_op(b, OP_DELETE, pos + off, tmp, c)) {
            return 0;
        }
        off += c;
    }
    if (!raw_delete(b, pos, n)) {
        return 0;
    }
    b->dirty = 1;
    return 1;
}

int edit_undo(int id) {
    EditBuf *b = buf_of(id);
    int group;
    int any = 0;
    if (!b || b->op_at <= 0) {
        return 0;
    }
    group = b->ops[b->op_at - 1].group;
    while (b->op_at > 0 && b->ops[b->op_at - 1].group == group) {
        b->op_at--;
        if (!apply_back(b, &b->ops[b->op_at])) {
            return 0;
        }
        any = 1;
    }
    if (any) {
        b->dirty = 1;
    }
    return any;
}

int edit_redo(int id) {
    EditBuf *b = buf_of(id);
    int group;
    int any = 0;
    if (!b || b->op_at >= b->op_len) {
        return 0;
    }
    group = b->ops[b->op_at].group;
    while (b->op_at < b->op_len && b->ops[b->op_at].group == group) {
        if (!apply_forward(b, &b->ops[b->op_at])) {
            return 0;
        }
        b->op_at++;
        any = 1;
    }
    if (any) {
        b->dirty = 1;
    }
    return any;
}

int edit_search(int id, const char *pat, int from) {
    EditBuf *b = buf_of(id);
    int n;
    int len;
    int i;
    if (!b || !pat || from < 0) {
        return -1;
    }
    n = 0;
    while (pat[n]) {
        ++n;
    }
    len = logical_len(b);
    if (n == 0 || from > len) {
        return -1;
    }
    for (i = from; i + n <= len; ++i) {
        int k;
        int ok = 1;
        for (k = 0; k < n; ++k) {
            if (char_at(b, i + k) != pat[k]) {
                ok = 0;
                break;
            }
        }
        if (ok) {
            return i;
        }
    }
    return -1;
}

int edit_replace(int id, const char *pat, const char *rep, int all) {
    EditBuf *b = buf_of(id);
    int from = 0;
    int count = 0;
    int plen;
    int group;
    if (!b || !pat || !rep) {
        return -1;
    }
    plen = 0;
    while (pat[plen]) {
        ++plen;
    }
    if (plen == 0) {
        return 0;
    }
    group = g_group + 1;
    while (1) {
        int at = edit_search(id, pat, from);
        int rlen;
        int saved;
        if (at < 0) {
            break;
        }
        rlen = 0;
        while (rep[rlen]) {
            ++rlen;
        }
        saved = g_group;
        if (!edit_delete(id, at, plen) || !edit_insert(id, at, rep)) {
            return -1;
        }
        /* delete and insert each opened a group. Fold them into one. */
        {
            int i;
            for (i = 0; i < b->op_at; ++i) {
                if (b->ops[i].group > group) {
                    b->ops[i].group = group;
                }
            }
        }
        g_group = saved > group ? saved : group;
        from = at + rlen;
        ++count;
        if (!all) {
            break;
        }
        if (rlen == 0) {
            from = at + 1;
            if (from > logical_len(b)) {
                break;
            }
        }
    }
    (void)group;
    return count;
}

int edit_line_offset(int id, int line) {
    EditBuf *b = buf_of(id);
    int i;
    int n;
    int len;
    if (!b || line < 1) {
        return -1;
    }
    if (line == 1) {
        return 0;
    }
    len = logical_len(b);
    n = 1;
    for (i = 0; i < len; ++i) {
        if (char_at(b, i) == '\n') {
            ++n;
            if (n == line) {
                return i + 1;
            }
        }
    }
    return -1;
}

int edit_copy(int id, char *out, int cap) {
    EditBuf *b = buf_of(id);
    int len;
    int i;
    int n;
    if (!b || !out || cap <= 0) {
        return -1;
    }
    len = logical_len(b);
    n = len;
    if (n >= cap) {
        n = cap - 1;
    }
    for (i = 0; i < n; ++i) {
        out[i] = char_at(b, i);
    }
    out[n] = 0;
    return n;
}
