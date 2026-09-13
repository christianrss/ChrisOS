#include "editor.h"

#include <stdio.h>
#include <string.h>

static int fail(const char *msg, const char *got) {
    fprintf(stderr, "FAIL %s\ngot:\n%s\n", msg, got);
    return 1;
}

int main(void) {
    Editor e;
    char buf[1024];

    ed_init(&e);
    ed_handle(&e, 'h');
    ed_handle(&e, 'i');
    ed_get_text(&e, buf, sizeof(buf));
    if (strcmp(buf, "hi") != 0) {
        return fail("insert hi", buf);
    }
    if (e.row != 0 || e.col != 2) {
        return fail("caret after hi", buf);
    }

    ed_handle(&e, 8);
    ed_get_text(&e, buf, sizeof(buf));
    if (strcmp(buf, "h") != 0) {
        return fail("backspace", buf);
    }

    ed_handle(&e, 'e');
    ed_handle(&e, 'l');
    ed_handle(&e, 'l');
    ed_handle(&e, 'o');
    ed_handle(&e, '\n');
    ed_handle(&e, 'x');
    ed_get_text(&e, buf, sizeof(buf));
    if (strcmp(buf, "hello\nx") != 0) {
        return fail("enter", buf);
    }
    if (e.row != 1 || e.col != 1) {
        return fail("caret after enter", buf);
    }

    ed_handle(&e, ED_UP);
    ed_handle(&e, ED_HOME);
    if (e.row != 0 || e.col != 0) {
        return fail("up+home", buf);
    }
    ed_handle(&e, ED_END);
    if (e.col != 5) {
        return fail("end", buf);
    }
    ed_handle(&e, ED_LEFT);
    ed_handle(&e, ED_DEL);
    ed_get_text(&e, buf, sizeof(buf));
    if (strcmp(buf, "hell\nx") != 0) {
        return fail("del last o", buf);
    }

    printf("host editor tests passed\n");
    return 0;
}