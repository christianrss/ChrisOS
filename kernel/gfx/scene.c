#include "scene.h"

#ifdef __freestanding__
#include "job.h"
#include "tri.h"
#endif

typedef struct SceneNode {
    int used;
    int mesh;
    int x;
    int y;
    int z;
    int yaw;
    int color;
} SceneNode;

typedef struct AnimKey {
    int used;
    int t;
    int x;
    int y;
    int z;
    int yaw;
} AnimKey;

static SceneNode g_node[SCENE_MAX];
static AnimKey g_key[16];
static int g_vis[SCENE_MAX];
static int g_vis_n;

int scene_add(int mesh, int x, int y, int z, int yaw, int color) {
    int i;
    for (i = 0; i < SCENE_MAX; ++i) {
        if (g_node[i].used)
            continue;
        g_node[i].used = 1;
        g_node[i].mesh = mesh;
        g_node[i].x = x;
        g_node[i].y = y;
        g_node[i].z = z;
        g_node[i].yaw = yaw;
        g_node[i].color = color;
        return i;
    }
    return -1;
}

void scene_clear(void) {
    int i;
    for (i = 0; i < SCENE_MAX; ++i)
        g_node[i].used = 0;
}

int scene_count(void) {
    int i;
    int n = 0;
    for (i = 0; i < SCENE_MAX; ++i) {
        if (g_node[i].used)
            n++;
    }
    return n;
}

int scene_in_frustum(int camx, int camz, int yaw, int x, int z, int radius) {
    int dx = x - camx;
    int dz = z - camz;
    int fwd;
    int side;
    if (radius < 1)
        radius = 1;
    if (yaw < 0)
        yaw = -yaw;
    yaw = yaw % 360;
    if (yaw < 45 || yaw >= 315) {
        fwd = dz;
        side = dx;
    } else if (yaw < 135) {
        fwd = dx;
        side = -dz;
    } else if (yaw < 225) {
        fwd = -dz;
        side = -dx;
    } else {
        fwd = -dx;
        side = dz;
    }
    if (fwd + radius < 8)
        return 0;
    if (side > fwd + radius || -side > fwd + radius)
        return 0;
    return 1;
}

int scene_visible(int camx, int camz, int yaw) {
    int i;
    g_vis_n = 0;
    for (i = 0; i < SCENE_MAX; ++i) {
        if (!g_node[i].used)
            continue;
        if (!scene_in_frustum(camx, camz, yaw, g_node[i].x, g_node[i].z, 16))
            continue;
        g_vis[g_vis_n++] = i;
    }
    return g_vis_n;
}

void anim_clear(void) {
    int i;
    for (i = 0; i < 16; ++i)
        g_key[i].used = 0;
}

int anim_key(int t, int x, int y, int z, int yaw) {
    int i;
    for (i = 0; i < 16; ++i) {
        if (g_key[i].used)
            continue;
        g_key[i].used = 1;
        g_key[i].t = t;
        g_key[i].x = x;
        g_key[i].y = y;
        g_key[i].z = z;
        g_key[i].yaw = yaw;
        return i;
    }
    return -1;
}

int anim_sample(int t, int *x, int *y, int *z, int *yaw) {
    int i;
    int best = -1;
    int next = -1;
    for (i = 0; i < 16; ++i) {
        if (!g_key[i].used)
            continue;
        if (g_key[i].t <= t && (best < 0 || g_key[i].t >= g_key[best].t))
            best = i;
        if (g_key[i].t >= t && (next < 0 || g_key[i].t <= g_key[next].t))
            next = i;
    }
    if (best < 0)
        return 0;
    if (!x || !y || !z || !yaw)
        return 0;
    if (next < 0 || next == best || g_key[next].t == g_key[best].t) {
        *x = g_key[best].x;
        *y = g_key[best].y;
        *z = g_key[best].z;
        *yaw = g_key[best].yaw;
        return 1;
    }
    {
        int span = g_key[next].t - g_key[best].t;
        int u = t - g_key[best].t;
        *x = g_key[best].x + (g_key[next].x - g_key[best].x) * u / span;
        *y = g_key[best].y + (g_key[next].y - g_key[best].y) * u / span;
        *z = g_key[best].z + (g_key[next].z - g_key[best].z) * u / span;
        *yaw = g_key[best].yaw + (g_key[next].yaw - g_key[best].yaw) * u / span;
    }
    return 1;
}

void anim_apply(int node, int t) {
    int x, y, z, yaw;
    if (node < 0 || node >= SCENE_MAX || !g_node[node].used)
        return;
    if (!anim_sample(t, &x, &y, &z, &yaw))
        return;
    g_node[node].x = x;
    g_node[node].y = y;
    g_node[node].z = z;
    g_node[node].yaw = yaw;
}

#ifdef __freestanding__
typedef struct SceneBand {
    uint32_t *pix;
    int w;
    int h;
    int y0;
    int y1;
    int camx;
    int camz;
    int yaw;
} SceneBand;

static SceneBand g_band[4];

static void draw_node(uint32_t *pix, int w, int h, int y0, int y1,
                      const SceneNode *n, int camx, int camz, int yaw) {
    int dx = n->x - camx;
    int dz = n->z - camz;
    int fwd;
    int side;
    int sz;
    int sx;
    int sy;
    int x0, yb, x1, yt;
    if (yaw < 0)
        yaw = -yaw;
    yaw = yaw % 360;
    if (yaw < 45 || yaw >= 315) {
        fwd = dz;
        side = dx;
    } else if (yaw < 135) {
        fwd = dx;
        side = -dz;
    } else if (yaw < 225) {
        fwd = -dz;
        side = -dx;
    } else {
        fwd = -dx;
        side = dz;
    }
    if (fwd < 8)
        return;
    sz = (16 * 80) / fwd;
    if (sz < 2)
        sz = 2;
    sx = w / 2 + (side * 80) / fwd;
    sy = h / 2 - (n->y * 80) / fwd;
    x0 = sx - sz;
    x1 = sx + sz;
    yb = sy + sz;
    yt = sy - sz;
    tri_fill_clip(pix, w, h, x0, yb, fwd, x1, yb, fwd, sx, yt, fwd,
                  n->color, 0, y0, w, y1);
}

static void band_job(void *arg, uint32_t cpu) {
    SceneBand *b = (SceneBand *)arg;
    int i;
    (void)cpu;
    for (i = 0; i < g_vis_n; ++i)
        draw_node(b->pix, b->w, b->h, b->y0, b->y1, &g_node[g_vis[i]],
                  b->camx, b->camz, b->yaw);
}

void scene_draw(uint32_t *pixels, int w, int h, int camx, int camz, int yaw) {
    int i;
    int bands = 4;
    if (!pixels || w <= 0 || h <= 0)
        return;
    scene_visible(camx, camz, yaw);
    if (h < bands)
        bands = 1;
    for (i = 0; i < bands; ++i) {
        g_band[i].pix = pixels;
        g_band[i].w = w;
        g_band[i].h = h;
        g_band[i].y0 = (h * i) / bands;
        g_band[i].y1 = (h * (i + 1)) / bands;
        g_band[i].camx = camx;
        g_band[i].camz = camz;
        g_band[i].yaw = yaw;
        if (!job_submit(band_job, &g_band[i]))
            band_job(&g_band[i], 0);
    }
    job_wait_idle();
}
#else
void scene_draw(uint32_t *pixels, int w, int h, int camx, int camz, int yaw) {
    (void)pixels;
    (void)w;
    (void)h;
    (void)camx;
    (void)camz;
    (void)yaw;
    scene_visible(camx, camz, yaw);
}
#endif
