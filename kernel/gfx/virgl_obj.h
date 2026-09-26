#ifndef CHRIS_VIRGL_OBJ_H
#define CHRIS_VIRGL_OBJ_H

#include <stdint.h>

/* VirGL object handles are not VirtIO resource ids and not ChrisOS public handles. */

#define VIRGL_OBJ_POOL_MAX 256

typedef struct VirglObjPool {
    uint32_t handle[VIRGL_OBJ_POOL_MAX];
    uint32_t type[VIRGL_OBJ_POOL_MAX];
    uint8_t live[VIRGL_OBJ_POOL_MAX];
    uint32_t next;
    int live_n;
    int peak_n;
} VirglObjPool;

void virgl_obj_init(VirglObjPool *p);
uint32_t virgl_obj_alloc(VirglObjPool *p, uint32_t type);
int virgl_obj_free(VirglObjPool *p, uint32_t handle);
int virgl_obj_live(const VirglObjPool *p, uint32_t handle);
int virgl_obj_live_count(const VirglObjPool *p);
int virgl_obj_peak(const VirglObjPool *p);

#endif
