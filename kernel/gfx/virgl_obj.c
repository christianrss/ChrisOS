#include "virgl_obj.h"

void virgl_obj_init(VirglObjPool *p) {
    int i;
    if (!p) {
        return;
    }
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        p->handle[i] = 0;
        p->type[i] = 0;
        p->live[i] = 0;
    }
    p->next = 1;
    p->live_n = 0;
    p->peak_n = 0;
}

static int handle_used(const VirglObjPool *p, uint32_t h) {
    int i;
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        if (p->live[i] && p->handle[i] == h) {
            return 1;
        }
    }
    return 0;
}

uint32_t virgl_obj_alloc(VirglObjPool *p, uint32_t type) {
    int slot = -1;
    int i;
    uint32_t spins = 0;
    if (!p) {
        return 0;
    }
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        if (!p->live[i]) {
            slot = i;
            break;
        }
    }
    if (slot < 0) {
        return 0;
    }
    while (p->next == 0u || handle_used(p, p->next)) {
        p->next++;
        if (p->next == 0u) {
            p->next = 1;
        }
        if (++spins > 100000u) {
            return 0;
        }
    }
    p->handle[slot] = p->next++;
    if (p->next == 0u) {
        p->next = 1;
    }
    p->type[slot] = type;
    p->live[slot] = 1;
    p->live_n++;
    if (p->live_n > p->peak_n) {
        p->peak_n = p->live_n;
    }
    return p->handle[slot];
}

int virgl_obj_free(VirglObjPool *p, uint32_t handle) {
    int i;
    if (!p || handle == 0u) {
        return -1;
    }
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        if (p->live[i] && p->handle[i] == handle) {
            p->live[i] = 0;
            p->handle[i] = 0;
            p->type[i] = 0;
            if (p->live_n > 0) {
                p->live_n--;
            }
            return 0;
        }
    }
    return -1;
}

int virgl_obj_live(const VirglObjPool *p, uint32_t handle) {
    int i;
    if (!p || handle == 0u) {
        return 0;
    }
    for (i = 0; i < VIRGL_OBJ_POOL_MAX; ++i) {
        if (p->live[i] && p->handle[i] == handle) {
            return 1;
        }
    }
    return 0;
}

int virgl_obj_live_count(const VirglObjPool *p) {
    return p ? p->live_n : 0;
}

int virgl_obj_peak(const VirglObjPool *p) {
    return p ? p->peak_n : 0;
}
