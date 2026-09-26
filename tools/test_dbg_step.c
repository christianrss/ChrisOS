#include "debug/dbg_session.h"

#include <stdio.h>

static int fail(const char *msg) {
    fprintf(stderr, "test_dbg_step: %s\n", msg);
    return 1;
}

static int pause_at(int mode, int start_line, int start_depth, const int *line,
                    const int *depth, int n) {
    int i;
    for (i = 0; i < n; ++i) {
        if (dbg_step_should_pause(mode, start_line, start_depth, line[i],
                                  depth[i])) {
            return i;
        }
    }
    return -1;
}

static int steps(void) {
    /* main line 10 calls foo. foo line 20 calls bar. bar is line 30. */
    const int into_line[] = {20, 21};
    const int into_depth[] = {2, 2};
    const int over_line[] = {20, 30, 31, 11};
    const int over_depth[] = {2, 3, 3, 1};
    const int out_line[] = {31, 21, 11};
    const int out_depth[] = {3, 2, 1};
    int at;

    at = pause_at(DBG_STEP_IN, 10, 1, into_line, into_depth, 2);
    if (at != 0) {
        return fail("step in should stop in the callee");
    }
    at = pause_at(DBG_STEP_OVER, 10, 1, over_line, over_depth, 4);
    if (at != 3) {
        return fail("step over should skip foo and bar");
    }
    at = pause_at(DBG_STEP_OUT, 30, 3, out_line, out_depth, 3);
    if (at != 1) {
        return fail("step out should stop in the caller");
    }
    if (dbg_step_should_pause(DBG_STEP_OUT, 30, 3, 31, 3)) {
        return fail("step out paused inside the same frame");
    }
    return 0;
}

static int sessions(void) {
    DbgMapRef map[3];
    const DbgSession *a;
    const DbgSession *b;
    int sa;
    int sb;
    int br;

    sa = dbg_session_create(1, 2);
    sb = dbg_session_create(2, 3);
    if (sa < 0 || sb < 0) {
        return fail("create");
    }
    if (dbg_session_add_break(sa, 2, 1, 4, 0) >= 0) {
        return fail("other owner added a breakpoint");
    }
    br = dbg_session_add_break(sa, 1, 1, 4, 1);
    if (br < 0 || dbg_session_add_break(sa, 1, 0, 8, 0) < 0) {
        return fail("add break");
    }
    map[0].pc = 10;
    map[0].file_id = 0;
    map[0].line = 8;
    map[1].pc = 40;
    map[1].file_id = 1;
    map[1].line = 4;
    map[2].pc = 40;
    map[2].file_id = 0;
    map[2].line = 4;
    if (dbg_session_resolve(sa, 1, map, 3) != 2) {
        return fail("resolve count");
    }
    a = dbg_session_get(sa, 1);
    if (!a || a->breaks[br].pc != 40 || a->breaks[br].file_id != 1) {
        return fail("resolved the wrong file");
    }
    if (dbg_session_on_pc(sa, 99)) {
        return fail("hit a pc with no breakpoint");
    }
    if (!dbg_session_on_pc(sa, 40)) {
        return fail("missed the breakpoint");
    }
    a = dbg_session_get(sa, 1);
    if (!a || a->breaks[br].used) {
        return fail("temporary breakpoint stayed enabled");
    }
    if (!dbg_session_on_pc(sa, 10)) {
        return fail("persistent breakpoint");
    }
    a = dbg_session_get(sa, 1);
    if (!a || !a->breaks[1].used) {
        return fail("persistent breakpoint was removed");
    }
    if (dbg_session_add_watch(sa, 2, 16, DBG_WATCH_INT) >= 0) {
        return fail("other owner added a watch");
    }
    if (dbg_session_add_watch(sa, 1, 16, DBG_WATCH_INT) < 0 ||
        dbg_session_add_watch(sa, 1, 32, DBG_WATCH_PTR) < 0) {
        return fail("watches");
    }
    a = dbg_session_get(sa, 1);
    if (!a || a->nwatch < 2 || a->watches[0].kind != DBG_WATCH_INT) {
        return fail("watch record");
    }
    if (!dbg_session_set_fault(sa, 1, 3, 40)) {
        return fail("fault");
    }
    b = dbg_session_get(sb, 2);
    if (!b || b->fault_type != 0 || b->state == DBG_FAULTED) {
        return fail("fault leaked into the other session");
    }
    a = dbg_session_get(sa, 1);
    if (!a || a->state != DBG_FAULTED || a->fault_pc != 40) {
        return fail("fault record");
    }
    if (!dbg_session_release(sa, 1) || dbg_session_get(sa, 1)) {
        return fail("release");
    }
    if (!dbg_session_release(sb, -1)) {
        return fail("kernel release");
    }
    return 0;
}

int main(void) {
    if (steps() || sessions()) {
        return 1;
    }
    puts("test_dbg_step: ok");
    return 0;
}
