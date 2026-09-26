#include "chrismake.h"
#include <string.h>

#define MK_VARS 32
#define MK_RULES 48
#define MK_PRE 12
#define MK_REC 6
#define MK_PATH 96
#define MK_LINE 192
#define MK_VAR_NAME 32
#define MK_VAR_VAL 128

typedef struct {
    char name[MK_VAR_NAME];
    char value[MK_VAR_VAL];
} MkVar;

typedef struct {
    char target[MK_PATH];
    char prereq[MK_PRE][MK_PATH];
    int npre;
    char recipe[MK_REC][MK_LINE];
    int nrec;
    int phony;
} MkRule;

typedef struct {
    MkVar vars[MK_VARS];
    int nvars;
    MkRule rules[MK_RULES];
    int nrules;
    int visiting[MK_RULES];
    ChrisMakeRecipeFn run;
    ChrisMakeStampFn stamp;
    void *user;
} MkFile;

static int mk_len(const char *s) {
    int n = 0;
    if (!s) {
        return 0;
    }
    while (s[n]) {
        n++;
    }
    return n;
}

static int mk_eq(const char *a, const char *b) {
    int i = 0;
    if (!a || !b) {
        return 0;
    }
    while (a[i] && b[i] && a[i] == b[i]) {
        i++;
    }
    return a[i] == 0 && b[i] == 0;
}

static void mk_copy(char *dst, int cap, const char *src) {
    int i = 0;
    if (cap < 1) {
        return;
    }
    if (!src) {
        dst[0] = 0;
        return;
    }
    while (src[i] && i < cap - 1) {
        dst[i] = src[i];
        i++;
    }
    dst[i] = 0;
}

static void mk_err(char *err, int cap, const char *msg) {
    mk_copy(err, cap, msg ? msg : "make fail");
}

static void mk_trim(char *s) {
    int n;
    int i;
    int j;
    if (!s) {
        return;
    }
    n = mk_len(s);
    while (n > 0 && (s[n - 1] == ' ' || s[n - 1] == '\t' || s[n - 1] == '\r')) {
        s[--n] = 0;
    }
    i = 0;
    while (s[i] == ' ' || s[i] == '\t') {
        i++;
    }
    if (i == 0) {
        return;
    }
    j = 0;
    while (s[i]) {
        s[j++] = s[i++];
    }
    s[j] = 0;
}

static int last_dot(const char *s) {
    int i;
    int d = -1;
    for (i = 0; s && s[i]; i++) {
        if (s[i] == '.') {
            d = i;
        }
    }
    return d;
}

static int is_clv_name(const char *s) {
    int n = mk_len(s);
    if (n < 5) {
        return 0;
    }
    return (s[n - 4] == '.' && s[n - 3] == 'C' && s[n - 2] == 'L' &&
            s[n - 1] == 'V') ||
           (s[n - 4] == '.' && s[n - 3] == 'c' && s[n - 2] == 'l' &&
            s[n - 1] == 'v');
}

static int clv_to_lst(const char *clv, char *out, int cap) {
    int d;
    int i;
    int upper;
    if (!clv || !out || cap < 5) {
        return 0;
    }
    d = last_dot(clv);
    if (d <= 0) {
        return 0;
    }
    if (d + 4 >= cap) {
        return 0;
    }
    for (i = 0; i < d; i++) {
        out[i] = clv[i];
    }
    upper = clv[d + 1] >= 'A' && clv[d + 1] <= 'Z';
    out[d] = '.';
    if (upper) {
        out[d + 1] = 'L';
        out[d + 2] = 'S';
        out[d + 3] = 'T';
    } else {
        out[d + 1] = 'l';
        out[d + 2] = 's';
        out[d + 3] = 't';
    }
    out[d + 4] = 0;
    return 1;
}

static const char *var_get(const MkFile *m, const char *name) {
    int i;
    for (i = 0; i < m->nvars; i++) {
        if (mk_eq(m->vars[i].name, name)) {
            return m->vars[i].value;
        }
    }
    return 0;
}

static int var_set(MkFile *m, const char *name, const char *value) {
    int i;
    if (!name || !name[0] || m->nvars >= MK_VARS) {
        return 0;
    }
    for (i = 0; i < m->nvars; i++) {
        if (mk_eq(m->vars[i].name, name)) {
            mk_copy(m->vars[i].value, MK_VAR_VAL, value);
            return 1;
        }
    }
    mk_copy(m->vars[m->nvars].name, MK_VAR_NAME, name);
    mk_copy(m->vars[m->nvars].value, MK_VAR_VAL, value);
    m->nvars++;
    return 1;
}

static int expand(const MkFile *m, const char *in, char *out, int cap,
                  const char *at, const char *lt) {
    int i = 0;
    int o = 0;
    if (!out || cap < 1) {
        return 0;
    }
    out[0] = 0;
    if (!in) {
        return 1;
    }
    while (in[i] && o < cap - 1) {
        if (in[i] == '$' && in[i + 1] == '$') {
            out[o++] = '$';
            i += 2;
            continue;
        }
        if (in[i] == '$' && in[i + 1] == '@') {
            int k = 0;
            while (at && at[k] && o < cap - 1) {
                out[o++] = at[k++];
            }
            i += 2;
            continue;
        }
        if (in[i] == '$' && in[i + 1] == '<') {
            int k = 0;
            while (lt && lt[k] && o < cap - 1) {
                out[o++] = lt[k++];
            }
            i += 2;
            continue;
        }
        if (in[i] == '$' && in[i + 1] == '(') {
            char name[MK_VAR_NAME];
            int n = 0;
            const char *val;
            i += 2;
            while (in[i] && in[i] != ')' && n < MK_VAR_NAME - 1) {
                name[n++] = in[i++];
            }
            name[n] = 0;
            if (in[i] == ')') {
                i++;
            }
            val = var_get(m, name);
            if (val) {
                int k = 0;
                while (val[k] && o < cap - 1) {
                    out[o++] = val[k++];
                }
            }
            continue;
        }
        out[o++] = in[i++];
    }
    out[o] = 0;
    return 1;
}

static int find_rule(const MkFile *m, const char *name) {
    int i;
    for (i = 0; i < m->nrules; i++) {
        if (mk_eq(m->rules[i].target, name)) {
            return i;
        }
    }
    return -1;
}

static void mark_phony(MkFile *m, const char *name) {
    int ri;
    if (!name || !name[0]) {
        return;
    }
    ri = find_rule(m, name);
    if (ri >= 0) {
        m->rules[ri].phony = 1;
        return;
    }
    if (m->nrules >= MK_RULES) {
        return;
    }
    memset(&m->rules[m->nrules], 0, sizeof(m->rules[0]));
    mk_copy(m->rules[m->nrules].target, MK_PATH, name);
    m->rules[m->nrules].phony = 1;
    m->nrules++;
}

static void add_tokens_phony(MkFile *m, const char *line) {
    char tok[MK_PATH];
    int i = 0;
    int j;
    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        if (!line[i]) {
            break;
        }
        j = 0;
        while (line[i] && line[i] != ' ' && line[i] != '\t' && j < MK_PATH - 1) {
            tok[j++] = line[i++];
        }
        tok[j] = 0;
        mark_phony(m, tok);
    }
}

static int add_prereqs(MkRule *r, const char *line) {
    char tok[MK_PATH];
    int i = 0;
    int j;
    while (line[i]) {
        while (line[i] == ' ' || line[i] == '\t') {
            i++;
        }
        if (!line[i]) {
            break;
        }
        j = 0;
        while (line[i] && line[i] != ' ' && line[i] != '\t' && j < MK_PATH - 1) {
            tok[j++] = line[i++];
        }
        tok[j] = 0;
        if (r->npre >= MK_PRE) {
            return 0;
        }
        mk_copy(r->prereq[r->npre], MK_PATH, tok);
        r->npre++;
    }
    return 1;
}

static int parse_mk(const char *text, MkFile *m, char *err, int err_cap) {
    char line[MK_LINE];
    int li = 0;
    int cur = -1;
    const char *p;
    memset(m, 0, sizeof(*m));
    if (!text) {
        mk_err(err, err_cap, "make: empty");
        return 0;
    }
    p = text;
    while (*p || li) {
        char c = *p ? *p++ : '\n';
        if (c == '\r') {
            continue;
        }
        if (c != '\n' && li < MK_LINE - 1) {
            line[li++] = c;
            continue;
        }
        line[li] = 0;
        li = 0;
        if (line[0] == '\t') {
            MkRule *r;
            if (cur < 0) {
                mk_err(err, err_cap, "make: recipe before rule");
                return 0;
            }
            r = &m->rules[cur];
            if (r->nrec >= MK_REC) {
                mk_err(err, err_cap, "make: too many recipes");
                return 0;
            }
            mk_copy(r->recipe[r->nrec], MK_LINE, line + 1);
            mk_trim(r->recipe[r->nrec]);
            if (r->recipe[r->nrec][0]) {
                r->nrec++;
            }
            continue;
        }
        mk_trim(line);
        if (!line[0] || line[0] == '#') {
            cur = -1;
            continue;
        }
        if (line[0] == '.' && line[1] == 'P' && line[2] == 'H' &&
            line[3] == 'O' && line[4] == 'N' && line[5] == 'Y') {
            const char *col = line;
            while (*col && *col != ':') {
                col++;
            }
            if (*col == ':') {
                add_tokens_phony(m, col + 1);
            }
            cur = -1;
            continue;
        }
        {
            int eq = -1;
            int col = -1;
            int i;
            for (i = 0; line[i]; i++) {
                if (eq < 0 && line[i] == '=') {
                    eq = i;
                }
                if (col < 0 && line[i] == ':') {
                    col = i;
                }
            }
            if (eq >= 0 && (col < 0 || eq < col)) {
                char name[MK_VAR_NAME];
                int n = eq;
                int vi;
                if (n >= MK_VAR_NAME) {
                    n = MK_VAR_NAME - 1;
                }
                for (vi = 0; vi < n; vi++) {
                    name[vi] = line[vi];
                }
                name[n] = 0;
                mk_trim(name);
                mk_trim(line + eq + 1);
                if (!var_set(m, name, line + eq + 1)) {
                    mk_err(err, err_cap, "make: too many vars");
                    return 0;
                }
                cur = -1;
                continue;
            }
            if (col < 0) {
                mk_err(err, err_cap, "make: bad line");
                return 0;
            }
            {
                int ri;
                line[col] = 0;
                mk_trim(line);
                ri = find_rule(m, line);
                if (ri < 0) {
                    if (m->nrules >= MK_RULES) {
                        mk_err(err, err_cap, "make: too many rules");
                        return 0;
                    }
                    memset(&m->rules[m->nrules], 0, sizeof(m->rules[0]));
                    mk_copy(m->rules[m->nrules].target, MK_PATH, line);
                    ri = m->nrules;
                    m->nrules++;
                }
                if (!add_prereqs(&m->rules[ri], line + col + 1)) {
                    mk_err(err, err_cap, "make: too many prereqs");
                    return 0;
                }
                cur = ri;
            }
        }
    }
    if (m->nrules < 1) {
        mk_err(err, err_cap, "make: no targets");
        return 0;
    }
    return 1;
}

static int first_target(const MkFile *m, char *out, int cap) {
    int i;
    for (i = 0; i < m->nrules; i++) {
        if (!mk_eq(m->rules[i].target, ".PHONY")) {
            mk_copy(out, cap, m->rules[i].target);
            return 1;
        }
    }
    return 0;
}

static int build_target(MkFile *m, const char *name, int depth, char *err,
                        int err_cap);

static int run_expanded(MkFile *m, const char *recipe, const char *at,
                        const char *lt, char *err, int err_cap) {
    char exp[MK_LINE];
    expand(m, recipe, exp, MK_LINE, at, lt);
    if (!exp[0]) {
        return 1;
    }
    if (!m->run) {
        return 1;
    }
    if (!m->run(m->user, exp, err, err_cap)) {
        if (err && err_cap > 0 && !err[0]) {
            mk_err(err, err_cap, "make: recipe fail");
        }
        return 0;
    }
    return 1;
}

static int build_target(MkFile *m, const char *name, int depth, char *err,
                        int err_cap) {
    int ri;
    int i;
    const char *lt;
    if (!name || !name[0]) {
        return 1;
    }
    if (depth > 32) {
        mk_err(err, err_cap, "make: too deep");
        return 0;
    }
    ri = find_rule(m, name);
    if (ri < 0) {
        if (is_clv_name(name)) {
            char src[MK_PATH];
            char rec[MK_LINE];
            if (!clv_to_lst(name, src, MK_PATH)) {
                return 1;
            }
            rec[0] = 'c';
            rec[1] = 'c';
            rec[2] = ' ';
            rec[3] = '-';
            rec[4] = 'c';
            rec[5] = ' ';
            mk_copy(rec + 6, MK_LINE - 6, src);
            return run_expanded(m, rec, name, src, err, err_cap);
        }
        return 1;
    }
    if (m->visiting[ri]) {
        mk_err(err, err_cap, "make: cycle");
        return 0;
    }
    m->visiting[ri] = 1;
    for (i = 0; i < m->rules[ri].npre; i++) {
        char pre[MK_PATH];
        expand(m, m->rules[ri].prereq[i], pre, MK_PATH, name, 0);
        if (!build_target(m, pre, depth + 1, err, err_cap)) {
            m->visiting[ri] = 0;
            return 0;
        }
    }
    if (!m->rules[ri].phony && m->stamp) {
        uint64_t tm = 0;
        int fresh = 1;
        if (m->stamp(m->user, name, &tm) != 0) {
            fresh = 0;
        }
        for (i = 0; fresh && i < m->rules[ri].npre; i++) {
            char pre[MK_PATH];
            uint64_t pm = 0;
            expand(m, m->rules[ri].prereq[i], pre, MK_PATH, name, 0);
            if (m->stamp(m->user, pre, &pm) != 0 || pm > tm) {
                fresh = 0;
            }
        }
        if (fresh) {
            m->visiting[ri] = 0;
            return 1;
        }
    }
    lt = m->rules[ri].npre ? m->rules[ri].prereq[0] : "";
    {
        char lt_exp[MK_PATH];
        expand(m, lt, lt_exp, MK_PATH, name, 0);
        if (m->rules[ri].nrec == 0 && is_clv_name(name) && lt_exp[0]) {
            char rec[MK_LINE];
            rec[0] = 'c';
            rec[1] = 'c';
            rec[2] = ' ';
            rec[3] = '-';
            rec[4] = 'c';
            rec[5] = ' ';
            mk_copy(rec + 6, MK_LINE - 6, lt_exp);
            if (!run_expanded(m, rec, name, lt_exp, err, err_cap)) {
                m->visiting[ri] = 0;
                return 0;
            }
        }
        for (i = 0; i < m->rules[ri].nrec; i++) {
            if (!run_expanded(m, m->rules[ri].recipe[i], name, lt_exp, err,
                              err_cap)) {
                m->visiting[ri] = 0;
                return 0;
            }
        }
    }
    m->visiting[ri] = 0;
    return 1;
}

int chrismake_run_stamped(const char *text, const char *target,
                          ChrisMakeRecipeFn run, ChrisMakeStampFn stamp,
                          void *user, char *err, int err_cap) {
    MkFile m;
    char want[MK_PATH];
    if (err && err_cap > 0) {
        err[0] = 0;
    }
    if (!parse_mk(text, &m, err, err_cap)) {
        return 0;
    }
    m.run = run;
    m.stamp = stamp;
    m.user = user;
    if (target && target[0]) {
        mk_copy(want, MK_PATH, target);
        mk_trim(want);
    } else if (!first_target(&m, want, MK_PATH)) {
        mk_err(err, err_cap, "make: no targets");
        return 0;
    }
    return build_target(&m, want, 0, err, err_cap);
}

int chrismake_run(const char *text, const char *target, ChrisMakeRecipeFn run,
                  void *user, char *err, int err_cap) {
    return chrismake_run_stamped(text, target, run, 0, user, err, err_cap);
}

int chrismake_first_recipe(const char *text, const char *target, char *out,
                           int cap) {
    MkFile m;
    char dummy[CHRISMAKE_MSG];
    int ri;
    const char *lt;
    if (out && cap > 0) {
        out[0] = 0;
    }
    if (!parse_mk(text, &m, dummy, (int)sizeof(dummy))) {
        return 0;
    }
    if (!target || !target[0]) {
        if (!first_target(&m, dummy, (int)sizeof(dummy))) {
            return 0;
        }
        target = dummy;
    }
    ri = find_rule(&m, target);
    if (ri < 0) {
        return 0;
    }
    lt = m.rules[ri].npre ? m.rules[ri].prereq[0] : "";
    if (m.rules[ri].nrec < 1) {
        return 0;
    }
    expand(&m, m.rules[ri].recipe[0], out, cap, m.rules[ri].target, lt);
    return out && out[0] ? 1 : 0;
}
