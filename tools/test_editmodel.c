#include "edit/editmodel.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *msg) {
    fprintf(stderr, "test_editmodel: %s\n", msg);
    return 1;
}

static int text_is(int id, const char *expect) {
    char buf[256];
    if (edit_copy(id, buf, (int)sizeof(buf)) < 0) {
        return 0;
    }
    return strcmp(buf, expect) == 0;
}

int main(void) {
    int a;
    int b;
    int c;
    int d;
    int e;
    char big[5001];
    char out[5002];
    int i;

    edit_reset();
    a = edit_open("SRC/MAIN.CC");
    b = edit_open("LIB/SIM.CC");
    if (a < 0 || b < 0 || edit_active() != b) {
        return fail("open");
    }
    if (!edit_switch(a) || edit_active() != a) {
        return fail("switch");
    }
    if (!edit_insert(a, 0, "one\ntwo\nthree") || edit_line_offset(a, 3) != 8) {
        return fail("goto line");
    }
    if (edit_search(a, "two", 0) != 4) {
        return fail("search");
    }
    if (edit_replace(a, "two", "TWO", 0) != 1 || !text_is(a, "one\nTWO\nthree")) {
        return fail("replace");
    }
    if (!edit_undo(a) || !text_is(a, "one\ntwo\nthree")) {
        return fail("undo replace");
    }
    if (!edit_redo(a) || !text_is(a, "one\nTWO\nthree")) {
        return fail("redo replace");
    }
    if (!edit_insert(b, 0, "alpha beta alpha") ||
        edit_replace(b, "alpha", "a", 1) != 2 || !text_is(b, "a beta a")) {
        return fail("replace all");
    }
    if (!edit_undo(b) || !text_is(b, "alpha beta alpha")) {
        return fail("undo replace all");
    }
    if (!edit_dirty(a) || edit_close(a, 0) != -2) {
        return fail("dirty close");
    }
    if (!edit_mark_clean(a) || edit_close(a, 0) != 0) {
        return fail("clean close");
    }
    if (edit_switch(a)) {
        return fail("closed buffer still switches");
    }
    c = edit_open("c");
    d = edit_open("d");
    e = edit_open("e");
    if (c < 0 || d < 0 || e < 0 || edit_open("f") >= 0) {
        return fail("buffer cap");
    }
    (void)d;
    for (i = 0; i < 5000; ++i) {
        big[i] = (char)('A' + (i % 26));
    }
    big[5000] = 0;
    if (!edit_insert(c, 0, big) || edit_length(c) != 5000) {
        return fail("large insert");
    }
    if (edit_copy(c, out, (int)sizeof(out)) < 5000 || out[0] != 'A' ||
        out[4999] != big[4999]) {
        return fail("large bytes");
    }
    if (!edit_undo(c) || edit_length(c) != 0) {
        return fail("undo large");
    }
    puts("test_editmodel: ok");
    return 0;
}
