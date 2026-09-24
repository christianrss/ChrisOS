#ifndef CHRIS_DBG_SESSION_H
#define CHRIS_DBG_SESSION_H

#include <stdint.h>

enum {
    DBG_STEP_NONE = 0,
    DBG_STEP_IN = 1,
    DBG_STEP_OVER = 2,
    DBG_STEP_OUT = 3
};

enum {
    DBG_CREATED = 1,
    DBG_RUNNING = 2,
    DBG_PAUSED = 3,
    DBG_WAITING = 4,
    DBG_HALTED = 5,
    DBG_FAULTED = 6,
    DBG_STOPPED = 7
};

enum {
    DBG_WATCH_MEM = 1,
    DBG_WATCH_INT = 2,
    DBG_WATCH_FLOAT = 3,
    DBG_WATCH_PTR = 4
};

#define DBG_SESSION_MAX 8
#define DBG_BREAK_MAX 32
#define DBG_WATCH_MAX 8

typedef struct DbgBreak {
    uint16_t file_id;
    uint16_t line;
    uint32_t pc;
    uint8_t used;
    uint8_t enabled;
    uint8_t temporary;
    uint8_t resolved;
} DbgBreak;

typedef struct DbgWatch {
    uint32_t addr;
    uint8_t kind;
    uint8_t used;
} DbgWatch;

typedef struct DbgSession {
    int id;
    int used;
    int owner;
    int slot;
    int state;
    int step_mode;
    int step_depth;
    int step_line;
    int nbreak;
    int nwatch;
    int fault_type;
    uint32_t fault_pc;
    DbgBreak breaks[DBG_BREAK_MAX];
    DbgWatch watches[DBG_WATCH_MAX];
} DbgSession;

typedef struct DbgMapRef {
    uint32_t pc;
    uint16_t file_id;
    uint16_t line;
} DbgMapRef;

/* caller < 0 is the kernel. Any other caller must own the session. */
int dbg_session_create(int owner, int slot);
int dbg_session_release(int id, int caller);
const DbgSession *dbg_session_get(int id, int caller);
int dbg_session_set_state(int id, int caller, int state);
int dbg_session_add_break(int id, int caller, uint16_t file_id, uint16_t line,
                          int temporary);
int dbg_session_resolve(int id, int caller, const DbgMapRef *map, int nmap);
int dbg_session_on_pc(int id, uint32_t pc);
int dbg_session_add_watch(int id, int caller, uint32_t addr, int kind);
int dbg_session_set_fault(int id, int caller, int type, uint32_t pc);

int dbg_step_should_pause(int mode, int start_line, int start_depth, int line,
                          int depth);

#endif
