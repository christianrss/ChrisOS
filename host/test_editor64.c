#include <editor.h>

#include <stdio.h>
#include <string.h>

static int g_fails;

static void expect_int(const char *name, int got, int want) {
    if (got != want) {
        fprintf(stderr, "FAIL %s: got %d want %d\n", name, got, want);
        g_fails++;
    }
}

static void expect_str(const char *name, const char *got, const char *want) {
    if (strcmp(got, want) != 0) {
        fprintf(stderr, "FAIL %s: got \"%s\" want \"%s\"\n", name, got, want);
        g_fails++;
    }
}

static void test_insert_and_get(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    expect_int("insert a", ed_handle(&e, 'a'), 1);
    expect_int("insert b", ed_handle(&e, 'b'), 1);
    expect_int("insert c", ed_handle(&e, 'c'), 1);
    expect_int("row after abc", e.row, 0);
    expect_int("col after abc", e.col, 3);
    expect_int("len abc", ed_length(&e, 0), 3);
    expect_int("dirty after insert", ed_is_dirty(&e), 1);
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("text abc", out, "abc");
}

static void test_backspace_middle(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    ed_handle(&e, 'a');
    ed_handle(&e, 'b');
    ed_handle(&e, 'c');
    ed_handle(&e, ED_LEFT);
    expect_int("bs middle", ed_handle(&e, 8), 1);
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("text ac", out, "ac");
    expect_int("col after bs", e.col, 1);
}

static void test_enter(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    ed_handle(&e, 'a');
    ed_handle(&e, 'b');
    expect_int("enter", ed_handle(&e, '\n'), 1);
    ed_handle(&e, 'c');
    expect_int("nlines enter", e.nlines, 2);
    expect_int("row after enter", e.row, 1);
    expect_int("col after enter", e.col, 1);
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("text ab\\nc", out, "ab\nc");
}

static void test_ed_up_returns(void) {
    static Editor e;
    int rc;
    ed_init(&e);
    ed_handle(&e, 'a');
    ed_handle(&e, '\n');
    ed_handle(&e, 'b');
    ed_handle(&e, '\n');
    ed_handle(&e, 'c');
    expect_int("row before up", e.row, 2);
    rc = ed_handle(&e, ED_UP);
    expect_int("ED_UP rc", rc, 1);
    expect_int("row after first up", e.row, 1);
    rc = ed_handle(&e, ED_UP);
    expect_int("ED_UP rc 2", rc, 1);
    expect_int("row after second up", e.row, 0);
    rc = ed_handle(&e, ED_UP);
    expect_int("ED_UP rc top", rc, 1);
    expect_int("row stays 0", e.row, 0);
}

static void test_join_backspace(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    ed_handle(&e, 'a');
    ed_handle(&e, 'b');
    ed_handle(&e, '\n');
    ed_handle(&e, 'c');
    ed_handle(&e, 'd');
    ed_handle(&e, ED_HOME);
    expect_int("col 0 before join bs", e.col, 0);
    expect_int("row 1 before join bs", e.row, 1);
    expect_int("join bs", ed_handle(&e, 8), 1);
    expect_int("nlines after join bs", e.nlines, 1);
    expect_int("row after join bs", e.row, 0);
    expect_int("col after join bs", e.col, 2);
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("joined abcd", out, "abcd");
}

static void test_join_delete(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    ed_handle(&e, 'a');
    ed_handle(&e, 'b');
    ed_handle(&e, '\n');
    ed_handle(&e, 'c');
    ed_handle(&e, 'd');
    ed_handle(&e, ED_UP);
    ed_handle(&e, ED_END);
    expect_int("col at eol", e.col, 2);
    expect_int("join del", ed_handle(&e, ED_DEL), 1);
    expect_int("nlines after join del", e.nlines, 1);
    expect_int("row after join del", e.row, 0);
    expect_int("col after join del", e.col, 2);
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("joined abcd del", out, "abcd");
}

static void test_get_text_newlines(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    ed_handle(&e, 'x');
    ed_handle(&e, '\n');
    ed_handle(&e, 'y');
    ed_handle(&e, '\n');
    ed_handle(&e, 'z');
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("xyz newlines", out, "x\ny\nz");
    expect_int("no trailing nl len", (int)strlen(out), 5);
}

static void test_load_text(void) {
    static Editor e;
    char out[64];
    ed_init(&e);
    expect_int("load", ed_load_text(&e, "hi\nthere"), 1);
    expect_int("load nlines", e.nlines, 2);
    expect_int("load dirty", ed_is_dirty(&e), 0);
    expect_int("load row", e.row, 1);
    ed_get_text(&e, out, (int)sizeof(out));
    expect_str("load roundtrip", out, "hi\nthere");
}

static void test_init_clears_scroll_and_strings(void) {
    static Editor e;
    int i;
    memset(&e, 0x5A, sizeof(e));
    ed_init(&e);
    expect_int("init nlines", e.nlines, 1);
    expect_int("init row", e.row, 0);
    expect_int("init col", e.col, 0);
    expect_int("init dirty", e.dirty, 0);
    expect_int("init scroll_row", e.scroll_row, 0);
    expect_int("init scroll_col", e.scroll_col, 0);
    expect_int("init name0", (int)(unsigned char)e.name[0], 0);
    expect_int("init status0", (int)(unsigned char)e.status[0], 0);
    for (i = 0; i < ED_MAX_COLS; i++) {
        if (e.lines[0][i] != 0) {
            fprintf(stderr, "FAIL init line0[%d] not cleared\n", i);
            g_fails++;
            break;
        }
    }
}

static void test_name_status_api(void) {
    static Editor e;
    ed_init(&e);
    ed_set_name(&e, "NOTES.TXT");
    expect_str("ed_name", ed_name(&e), "NOTES.TXT");
    ed_set_status(&e, "hello status");
    expect_str("status", e.status, "hello status");
    expect_int("dirty before type", ed_is_dirty(&e), 0);
    ed_handle(&e, 'A');
    expect_int("dirty after type", ed_is_dirty(&e), 1);
}

static void test_line_full(void) {
    static Editor e;
    int i;
    int rc;
    ed_init(&e);
    for (i = 0; i < ED_MAX_COLS - 1; i++) {
        rc = ed_handle(&e, 'a');
        if (!rc) {
            fprintf(stderr, "FAIL line full too early at %d\n", i);
            g_fails++;
            return;
        }
    }
    expect_int("len full", ed_length(&e, 0), ED_MAX_COLS - 1);
    rc = ed_handle(&e, 'b');
    expect_int("line full rc", rc, 0);
    expect_str("line full status", e.status, "line full");
    expect_int("len still full", ed_length(&e, 0), ED_MAX_COLS - 1);
}

static void test_too_many_lines(void) {
    static Editor e;
    int i;
    int rc;
    ed_init(&e);
    for (i = 1; i < ED_MAX_LINES; i++) {
        rc = ed_handle(&e, '\n');
        if (!rc) {
            fprintf(stderr, "FAIL too many lines too early at %d\n", i);
            g_fails++;
            return;
        }
    }
    expect_int("nlines max", e.nlines, ED_MAX_LINES);
    rc = ed_handle(&e, '\n');
    expect_int("too many rc", rc, 0);
    expect_str("too many status", e.status, "too many lines");
    expect_int("nlines stays max", e.nlines, ED_MAX_LINES);
}

static void test_load_overflow_line(void) {
    static Editor e;
    char long_line[ED_MAX_COLS + 8];
    int i;
    for (i = 0; i < ED_MAX_COLS + 4; i++) {
        long_line[i] = 'x';
    }
    long_line[ED_MAX_COLS + 4] = 0;
    expect_int("load long", ed_load_text(&e, long_line), 0);
    expect_str("load long status", e.status, "line full");
}

static void test_load_too_many_lines(void) {
    static Editor e;
    static char blob[ED_MAX_LINES * 2 + 8];
    int i;
    int n = 0;
    for (i = 0; i < ED_MAX_LINES + 2; i++) {
        blob[n++] = 'a';
        blob[n++] = '\n';
    }
    blob[n] = 0;
    expect_int("load many", ed_load_text(&e, blob), 0);
    expect_str("load many status", e.status, "too many lines");
}

static void test_join_bs_line_full(void) {
    static Editor e;
    int i;
    ed_init(&e);
    for (i = 0; i < 80; i++) {
        ed_handle(&e, 'a');
    }
    ed_handle(&e, '\n');
    for (i = 0; i < 80; i++) {
        ed_handle(&e, 'b');
    }
    ed_handle(&e, ED_HOME);
    expect_int("join bs overflow", ed_handle(&e, 8), 0);
    expect_str("join bs overflow status", e.status, "line full");
    expect_int("join bs overflow nlines", e.nlines, 2);
}

int main(void) {
    test_init_clears_scroll_and_strings();
    test_insert_and_get();
    test_backspace_middle();
    test_enter();
    test_ed_up_returns();
    test_join_backspace();
    test_join_delete();
    test_get_text_newlines();
    test_load_text();
    test_name_status_api();
    test_line_full();
    test_too_many_lines();
    test_load_overflow_line();
    test_load_too_many_lines();
    test_join_bs_line_full();
    if (g_fails) {
        fprintf(stderr, "%d host editor tests failed\n", g_fails);
        return 1;
    }
    printf("host editor tests passed\n");
    return 0;
}
