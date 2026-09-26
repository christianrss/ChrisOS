#ifndef CHRISVM_H
#define CHRISVM_H

#include "chris_arch.h"

#include <stddef.h>
#include <stdint.h>

#define CHRIS_RAM_DEFAULT (16ull * 1024ull * 1024ull)
#define CHRIS_STACK_RSP 0x80000ull
#define CHRIS_GDT_PHYS 0x70000ull
#define CHRIS_PT_RESERVE 0x4000ull
#define CHRIS_SHUTDOWN_PORT 0x501u
#define CHRIS_FB_PHYS 0x02000000ull
#define CHRIS_FB_WIDTH 640u
#define CHRIS_FB_HEIGHT 480u

typedef struct ChrisConfig {
    uint64_t ram_size;
    const char *backend;
    int trace;
    int trace_memory;
    int trace_io;
    int trace_mmio;
    int deterministic;
    uint64_t max_steps;
    uint64_t break_rip;
    int has_break;
    int debug;
    int headless;
    const char *fb_dump;
} ChrisConfig;

typedef struct ChrisMachine ChrisMachine;

void chris_config_init(ChrisConfig *cfg);
int chris_config_from_args(ChrisConfig *cfg, int argc, char **argv, const char **image, char *err,
                           size_t errcap);

ChrisMachine *chris_machine_create(const ChrisConfig *cfg);
void chris_machine_destroy(ChrisMachine *m);

int chris_load_elf(ChrisMachine *m, const uint8_t *image, size_t n, char *err, size_t errcap);
int chris_boot(ChrisMachine *m, uint64_t entry, uint64_t rsp);
int chris_run(ChrisMachine *m, uint64_t max_steps);

int chris_write_ram(ChrisMachine *m, uint64_t pa, const void *src, size_t n);
int chris_read_ram(ChrisMachine *m, uint64_t pa, void *dst, size_t n);

void chris_set_gpr(ChrisMachine *m, int index, uint64_t value);
uint64_t chris_get_gpr(const ChrisMachine *m, int index);
uint64_t chris_get_rip(const ChrisMachine *m);
uint64_t chris_get_rflags(const ChrisMachine *m);
uint64_t chris_get_cr(const ChrisMachine *m, int n);
void chris_set_cr(ChrisMachine *m, int n, uint64_t value);

int chris_exit_reason(const ChrisMachine *m);
int chris_exception_vector(const ChrisMachine *m);
uint64_t chris_steps(const ChrisMachine *m);

const char *chris_serial_text(const ChrisMachine *m, size_t *len);
void chris_serial_set_hook(ChrisMachine *m, void (*hook)(void *ctx, char ch), void *ctx);

void chris_set_log(ChrisMachine *m, void (*log)(void *ctx, const char *line), void *ctx);
void chris_dump_cpu(const ChrisMachine *m);

int chris_fb_get(const ChrisMachine *m, uint32_t x, uint32_t y, uint32_t *pixel);
int chris_fb_dirty(const ChrisMachine *m);
int chris_fb_write_image(const ChrisMachine *m, const char *path);
int chris_view_show(const ChrisMachine *m, int milliseconds);

const ChrisCpuBackend *chris_backend_by_name(const char *name);

uint64_t chris_flags_bin(int alu, uint64_t a, uint64_t b, int os, uint64_t flags, uint64_t *result);

#endif
