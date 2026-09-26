#include "virgl_cmd.h"

#include "virgl_proto.h"

int virgl_cmd_init(VirglCmd *c, uint32_t *buf, uint32_t cap_dwords, uint32_t ctx) {
    if (!c || !buf || cap_dwords == 0u || ctx == 0u) {
        if (c) {
            c->err = -1;
            c->n = 0;
            c->d = 0;
            c->cap = 0;
        }
        return -1;
    }
    c->d = buf;
    c->cap = cap_dwords;
    c->n = 0;
    c->expect = 0;
    c->err = 0;
    c->ctx = ctx;
    return 0;
}

int virgl_cmd_ok(const VirglCmd *c) {
    return c && c->err == 0 && c->n > 0u;
}

uint32_t virgl_cmd_len(const VirglCmd *c) {
    return c ? c->n : 0u;
}

int virgl_cmd_u32(VirglCmd *c, uint32_t v) {
    if (!c || c->err || c->n >= c->cap || (c->expect != 0u && c->n >= c->expect)) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    c->d[c->n++] = v;
    return 0;
}

int virgl_cmd_begin(VirglCmd *c, uint32_t cmd, uint32_t obj, uint32_t len) {
    if (!c || c->err || len == 0u || c->n + 1u + len > c->cap) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (c->expect != 0u && c->n != c->expect) {
        c->err = -1;
        return -1;
    }
    c->d[c->n++] = VIRGL_CMD0(cmd, obj, len);
    c->expect = c->n + len;
    return 0;
}

int virgl_cmd_end(VirglCmd *c) {
    if (!c || c->err || c->n != c->expect) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    c->expect = 0;
    return 0;
}

int virgl_cmd_clear(VirglCmd *c, uint32_t buffers, uint32_t c0, uint32_t c1,
                    uint32_t c2, uint32_t c3, uint64_t depth_bits,
                    uint32_t stencil) {
    if (virgl_cmd_begin(c, VIRGL_CCMD_CLEAR, 0, VIRGL_OBJ_CLEAR_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, buffers);
    virgl_cmd_u32(c, c0);
    virgl_cmd_u32(c, c1);
    virgl_cmd_u32(c, c2);
    virgl_cmd_u32(c, c3);
    virgl_cmd_u32(c, (uint32_t)depth_bits);
    virgl_cmd_u32(c, (uint32_t)(depth_bits >> 32));
    virgl_cmd_u32(c, stencil);
    return virgl_cmd_end(c);
}

int virgl_cmd_surface(VirglCmd *c, uint32_t handle, uint32_t res, uint32_t fmt) {
    if (handle == 0u || res == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SURFACE,
                        VIRGL_OBJ_SURFACE_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, res);
    virgl_cmd_u32(c, fmt);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    return virgl_cmd_end(c);
}

int virgl_cmd_framebuffer(VirglCmd *c, uint32_t nr, uint32_t zs, uint32_t color) {
    if (nr != 1u || color == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_SET_FRAMEBUFFER_STATE, 0, nr + 2u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, nr);
    virgl_cmd_u32(c, zs);
    virgl_cmd_u32(c, color);
    return virgl_cmd_end(c);
}

int virgl_cmd_viewport(VirglCmd *c, uint32_t sx, uint32_t sy, uint32_t sz,
                       uint32_t tx, uint32_t ty, uint32_t tz) {
    if (virgl_cmd_begin(c, VIRGL_CCMD_SET_VIEWPORT_STATE, 0, 7u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, sx);
    virgl_cmd_u32(c, sy);
    virgl_cmd_u32(c, sz);
    virgl_cmd_u32(c, tx);
    virgl_cmd_u32(c, ty);
    virgl_cmd_u32(c, tz);
    return virgl_cmd_end(c);
}

int virgl_cmd_destroy(VirglCmd *c, uint32_t obj_type, uint32_t handle) {
    if (virgl_cmd_begin(c, VIRGL_CCMD_DESTROY_OBJECT, obj_type, 1u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    return virgl_cmd_end(c);
}

int virgl_cmd_bind(VirglCmd *c, uint32_t obj, uint32_t handle) {
    if (handle == 0u || obj == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_BIND_OBJECT, obj, 1u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    return virgl_cmd_end(c);
}

int virgl_cmd_blend_opaque(VirglCmd *c, uint32_t handle) {
    uint32_t i;
    if (handle == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_BLEND,
                        VIRGL_OBJ_BLEND_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0xfu << 27);
    for (i = 1; i < 8u; ++i) {
        virgl_cmd_u32(c, 0);
    }
    return virgl_cmd_end(c);
}

int virgl_cmd_dsa(VirglCmd *c, uint32_t handle, int depth_enable, uint32_t func) {
    uint32_t s0;
    if (handle == 0u || func > 7u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    s0 = 0;
    if (depth_enable) {
        s0 = 1u | (1u << 1) | ((func & 7u) << 2);
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_DSA,
                        VIRGL_OBJ_DSA_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, s0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    return virgl_cmd_end(c);
}

int virgl_cmd_raster(VirglCmd *c, uint32_t handle, uint32_t cull) {
    uint32_t s0;
    if (handle == 0u || cull > 3u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    s0 = (1u << 1) | ((cull & 3u) << 8) | (1u << 15) | (1u << 29) | (1u << 30);
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_RASTERIZER,
                        VIRGL_OBJ_RS_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, s0);
    virgl_cmd_u32(c, VIRGL_F32_ONE);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, VIRGL_F32_ONE);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    return virgl_cmd_end(c);
}

int virgl_cmd_velems(VirglCmd *c, uint32_t handle, uint32_t n,
                     const uint32_t *offset, const uint32_t *fmt) {
    uint32_t i;
    if (handle == 0u || n == 0u || n > 8u || !offset || !fmt) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_VERTEX_ELEMENTS,
                        n * 4u + 1u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    for (i = 0; i < n; ++i) {
        virgl_cmd_u32(c, offset[i]);
        virgl_cmd_u32(c, 0);
        virgl_cmd_u32(c, 0);
        virgl_cmd_u32(c, fmt[i]);
    }
    return virgl_cmd_end(c);
}

int virgl_cmd_vbuffers(VirglCmd *c, uint32_t stride, uint32_t offset,
                       uint32_t res) {
    if (res == 0u || stride == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_SET_VERTEX_BUFFERS, 0, 3u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, stride);
    virgl_cmd_u32(c, offset);
    virgl_cmd_u32(c, res);
    return virgl_cmd_end(c);
}

int virgl_cmd_ib(VirglCmd *c, uint32_t res, uint32_t index_size) {
    if (res == 0u || (index_size != 2u && index_size != 4u)) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_SET_INDEX_BUFFER, 0, 3u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, res);
    virgl_cmd_u32(c, index_size);
    virgl_cmd_u32(c, 0);
    return virgl_cmd_end(c);
}

int virgl_cmd_draw(VirglCmd *c, uint32_t start, uint32_t count, int indexed,
                   uint32_t min_index, uint32_t max_index) {
    if (count == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_DRAW_VBO, 0, VIRGL_DRAW_VBO_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, start);
    virgl_cmd_u32(c, count);
    virgl_cmd_u32(c, VIRGL_PRIM_TRIANGLES);
    virgl_cmd_u32(c, indexed ? 1u : 0u);
    virgl_cmd_u32(c, 1u);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, min_index);
    virgl_cmd_u32(c, max_index);
    virgl_cmd_u32(c, 0);
    return virgl_cmd_end(c);
}

int virgl_cmd_shader(VirglCmd *c, uint32_t handle, uint32_t stage, const char *text) {
    uint32_t slen;
    uint32_t sd;
    uint32_t i;
    uint32_t b;
    if (handle == 0u || !text || (stage != VIRGL_SHADER_VERTEX &&
                                  stage != VIRGL_SHADER_FRAGMENT)) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    slen = 0;
    while (text[slen] != 0) {
        slen++;
        if (slen > 3600u) {
            if (c) {
                c->err = -1;
            }
            return -1;
        }
    }
    slen++;
    sd = (slen + 3u) / 4u;
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SHADER,
                        5u + sd) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, stage);
    virgl_cmd_u32(c, slen);
    virgl_cmd_u32(c, slen > 128u ? (slen > 2048u ? 2048u : slen) : 128u);
    virgl_cmd_u32(c, 0);
    for (i = 0; i < sd; ++i) {
        uint32_t w = 0;
        for (b = 0; b < 4u; ++b) {
            uint32_t off = i * 4u + b;
            if (off < slen) {
                w |= (uint32_t)(uint8_t)text[off] << (8u * b);
            }
        }
        virgl_cmd_u32(c, w);
    }
    return virgl_cmd_end(c);
}

int virgl_cmd_link(VirglCmd *c, uint32_t vs, uint32_t fs) {
    if (vs == 0u || fs == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_BIND_SHADER, 0, 2u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, vs);
    virgl_cmd_u32(c, VIRGL_SHADER_VERTEX);
    if (virgl_cmd_end(c) != 0) {
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_BIND_SHADER, 0, 2u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, fs);
    virgl_cmd_u32(c, VIRGL_SHADER_FRAGMENT);
    if (virgl_cmd_end(c) != 0) {
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_LINK_SHADER, 0, VIRGL_LINK_SHADER_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, vs);
    virgl_cmd_u32(c, fs);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    return virgl_cmd_end(c);
}

int virgl_cmd_consts(VirglCmd *c, uint32_t stage, const uint32_t *words,
                     uint32_t nwords) {
    uint32_t i;
    if (!words || nwords == 0u || nwords > 64u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_SET_CONSTANT_BUFFER, 0, nwords + 2u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, stage);
    virgl_cmd_u32(c, 0);
    for (i = 0; i < nwords; ++i) {
        virgl_cmd_u32(c, words[i]);
    }
    return virgl_cmd_end(c);
}

int virgl_cmd_sampler(VirglCmd *c, uint32_t handle) {
    uint32_t i;
    uint32_t s0;
    if (handle == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    s0 = (2u) | (2u << 3) | (2u << 6) | (2u << 11);
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SAMPLER_STATE,
                        VIRGL_OBJ_SAMPLER_STATE_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, s0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0x447a0000u);
    for (i = 0; i < 4u; ++i) {
        virgl_cmd_u32(c, 0);
    }
    return virgl_cmd_end(c);
}

int virgl_cmd_sview(VirglCmd *c, uint32_t handle, uint32_t res, uint32_t fmt) {
    uint32_t sw = 0u | (1u << 3) | (2u << 6) | (3u << 9);
    if (handle == 0u || res == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_CREATE_OBJECT, VIRGL_OBJECT_SAMPLER_VIEW,
                        VIRGL_OBJ_SAMPLER_VIEW_SIZE) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, handle);
    virgl_cmd_u32(c, res);
    virgl_cmd_u32(c, fmt);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, sw);
    return virgl_cmd_end(c);
}

int virgl_cmd_bind_sampler(VirglCmd *c, uint32_t stage, uint32_t handle) {
    if (handle == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_BIND_SAMPLER_STATES, 0, 3u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, stage);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, handle);
    return virgl_cmd_end(c);
}

int virgl_cmd_set_views(VirglCmd *c, uint32_t stage, uint32_t handle) {
    if (handle == 0u) {
        if (c) {
            c->err = -1;
        }
        return -1;
    }
    if (virgl_cmd_begin(c, VIRGL_CCMD_SET_SAMPLER_VIEWS, 0, 3u) != 0) {
        return -1;
    }
    virgl_cmd_u32(c, stage);
    virgl_cmd_u32(c, 0);
    virgl_cmd_u32(c, handle);
    return virgl_cmd_end(c);
}
