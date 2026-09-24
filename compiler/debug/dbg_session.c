#include "debug/dbg_session.h"

#include <string.h>

static DbgSession g_sessions[DBG_SESSION_MAX];

static DbgSession *mut(int id, int caller) {
    DbgSession *s;
    if (id < 0 || id >= DBG_SESSION_MAX) {
        return 0;
    }
    s = &g_sessions[id];
    if (!s->used) {
        return 0;
    }
    if (caller >= 0 && s->owner != caller) {
        return 0;
    }
    return s;
}

int dbg_session_create(int owner, int slot) {
    int i;
    for (i = 0; i < DBG_SESSION_MAX; ++i) {
        if (!g_sessions[i].used) {
            memset(&g_sessions[i], 0, sizeof(g_sessions[i]));
            g_sessions[i].used = 1;
            g_sessions[i].id = i;
            g_sessions[i].owner = owner;
            g_sessions[i].slot = slot;
            g_sessions[i].state = DBG_CREATED;
            return i;
        }
    }
    return -1;
}

int dbg_session_release(int id, int caller) {
    DbgSession *s = mut(id, caller);
    if (!s) {
        return 0;
    }
    memset(s, 0, sizeof(*s));
    return 1;
}

const DbgSession *dbg_session_get(int id, int caller) {
    return mut(id, caller);
}

int dbg_session_set_state(int id, int caller, int state) {
    DbgSession *s = mut(id, caller);
    if (!s) {
        return 0;
    }
    s->state = state;
    return 1;
}

int dbg_session_add_break(int id, int caller, uint16_t file_id, uint16_t line,
                          int temporary) {
    DbgSession *s = mut(id, caller);
    int i;
    if (!s) {
        return -1;
    }
    for (i = 0; i < DBG_BREAK_MAX; ++i) {
        if (!s->breaks[i].used) {
            s->breaks[i].used = 1;
            s->breaks[i].enabled = 1;
            s->breaks[i].temporary = temporary ? 1 : 0;
            s->breaks[i].resolved = 0;
            s->breaks[i].file_id = file_id;
            s->breaks[i].line = line;
            s->breaks[i].pc = 0;
            if (i >= s->nbreak) {
                s->nbreak = i + 1;
            }
            return i;
        }
    }
    return -1;
}

int dbg_session_resolve(int id, int caller, const DbgMapRef *map, int nmap) {
    DbgSession *s = mut(id, caller);
    int i;
    int n = 0;
    if (!s || (!map && nmap > 0) || nmap < 0) {
        return -1;
    }
    for (i = 0; i < DBG_BREAK_MAX; ++i) {
        int k;
        if (!s->breaks[i].used) {
            continue;
        }
        s->breaks[i].resolved = 0;
        s->breaks[i].pc = 0;
        for (k = 0; k < nmap; ++k) {
            if (map[k].file_id == s->breaks[i].file_id &&
                map[k].line == s->breaks[i].line) {
                s->breaks[i].pc = map[k].pc;
                s->breaks[i].resolved = 1;
                ++n;
                break;
            }
        }
    }
    return n;
}

int dbg_session_on_pc(int id, uint32_t pc) {
    DbgSession *s = mut(id, -1);
    int i;
    if (!s) {
        return 0;
    }
    for (i = 0; i < DBG_BREAK_MAX; ++i) {
        if (!s->breaks[i].used || !s->breaks[i].enabled ||
            !s->breaks[i].resolved) {
            continue;
        }
        if (s->breaks[i].pc == pc) {
            if (s->breaks[i].temporary) {
                s->breaks[i].used = 0;
            }
            s->state = DBG_PAUSED;
            return 1;
        }
    }
    return 0;
}

int dbg_session_add_watch(int id, int caller, uint32_t addr, int kind) {
    DbgSession *s = mut(id, caller);
    int i;
    if (!s) {
        return -1;
    }
    if (kind != DBG_WATCH_MEM && kind != DBG_WATCH_INT &&
        kind != DBG_WATCH_FLOAT && kind != DBG_WATCH_PTR) {
        return -1;
    }
    for (i = 0; i < DBG_WATCH_MAX; ++i) {
        if (!s->watches[i].used) {
            s->watches[i].used = 1;
            s->watches[i].addr = addr;
            s->watches[i].kind = (uint8_t)kind;
            if (i >= s->nwatch) {
                s->nwatch = i + 1;
            }
            return i;
        }
    }
    return -1;
}

int dbg_session_set_fault(int id, int caller, int type, uint32_t pc) {
    DbgSession *s = mut(id, caller);
    if (!s) {
        return 0;
    }
    s->fault_type = type;
    s->fault_pc = pc;
    s->state = DBG_FAULTED;
    return 1;
}

int dbg_step_should_pause(int mode, int start_line, int start_depth, int line,
                          int depth) {
    if (mode == DBG_STEP_IN) {
        return line != start_line;
    }
    if (mode == DBG_STEP_OVER) {
        return depth <= start_depth && line != start_line;
    }
    if (mode == DBG_STEP_OUT) {
        return depth < start_depth;
    }
    return 0;
}
