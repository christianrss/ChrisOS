#ifndef CHRIS_MACHINE_INTERNAL_H
#define CHRIS_MACHINE_INTERNAL_H

#include "chrisvm.h"

#define CHRIS_IO_MAX 8
#define CHRIS_MMIO_MAX 8
#define CHRIS_TRACE_RING 256
#define CHRIS_TX_MAX 8192

typedef struct ChrisIoSlot {
    int used;
    uint16_t start;
    uint16_t end;
    int (*in)(void *ctx, uint16_t port, int size, uint32_t *value);
    int (*out)(void *ctx, uint16_t port, int size, uint32_t value);
    void *ctx;
} ChrisIoSlot;

typedef struct ChrisMmioSlot {
    int used;
    uint64_t base;
    uint64_t size;
    int (*read)(void *ctx, uint64_t offset, int size, uint64_t *value);
    int (*write)(void *ctx, uint64_t offset, int size, uint64_t value);
    void *ctx;
} ChrisMmioSlot;

typedef struct ChrisFb {
    uint64_t base;
    uint32_t width;
    uint32_t height;
    uint32_t pitch;
    uint8_t *pix;
    size_t size;
    int dirty;
} ChrisFb;

typedef struct ChrisSerial {
    uint8_t ier, lcr, mcr, scr, dll, dlm;
    uint8_t loop_data;
    int loop_dr;
    char tx[CHRIS_TX_MAX];
    size_t tx_len;
    void (*hook)(void *ctx, char ch);
    void *hook_ctx;
} ChrisSerial;

typedef struct ChrisTraceEnt {
    uint64_t rip;
    int len;
    uint8_t bytes[15];
    char text[96];
} ChrisTraceEnt;

typedef struct ChrisCpu {
    ChrisArchitectureState arch;
    struct ChrisMachine *machine;
    uint64_t steps;
    int halted;
    int exit_reason;
    int ex_vector;
    uint32_t ex_error;
    int delivering;
    int irq_pending;
    uint8_t irq_vector;
    int sti_delay;
    int rip_dirty;
    uint64_t tlb_gen;
    int trace;
    int trace_memory;
    int trace_io;
    int trace_mmio;
    uint64_t break_rip;
    int has_break;
    ChrisTraceEnt ring[CHRIS_TRACE_RING];
    int ring_i;
    int ring_n;
} ChrisCpu;

struct ChrisMachine {
    ChrisConfig cfg;
    uint8_t *ram;
    uint64_t ram_size;
    ChrisIoSlot io[CHRIS_IO_MAX];
    ChrisMmioSlot mmio[CHRIS_MMIO_MAX];
    ChrisSerial serial;
    ChrisFb fb;
    ChrisCpu *cpu;
    const ChrisCpuBackend *backend;
    uint64_t entry;
    int booted;
    int shutdown;
    void (*log)(void *ctx, const char *line);
    void *log_ctx;
};

int chris_io_map(ChrisMachine *m, uint16_t start, uint16_t end,
                 int (*in)(void *, uint16_t, int, uint32_t *),
                 int (*out)(void *, uint16_t, int, uint32_t), void *ctx);
int chris_io_in(ChrisMachine *m, uint16_t port, int size, uint32_t *value);
int chris_io_out(ChrisMachine *m, uint16_t port, int size, uint32_t value);

int chris_mmio_map(ChrisMachine *m, uint64_t base, uint64_t size,
                   int (*read)(void *, uint64_t, int, uint64_t *),
                   int (*write)(void *, uint64_t, int, uint64_t), void *ctx);
int chris_mmio_find(ChrisMachine *m, uint64_t pa, uint64_t *offset);

int chris_phys_read(ChrisMachine *m, uint64_t pa, void *dst, size_t n);
int chris_phys_write(ChrisMachine *m, uint64_t pa, const void *src, size_t n);

void chris_serial_attach(ChrisMachine *m);
int chris_fb_attach(ChrisMachine *m);

int chris_va_read(ChrisCpu *cpu, uint64_t va, void *dst, size_t n, int access);
int chris_va_write(ChrisCpu *cpu, uint64_t va, const void *src, size_t n);
int chris_translate(ChrisCpu *cpu, uint64_t va, uint64_t *pa, int access, uint32_t *err);

int chris_raise(ChrisCpu *cpu, int vector, int has_error, uint32_t error);
int chris_push8(ChrisCpu *cpu, uint64_t value);
int chris_pop8(ChrisCpu *cpu, uint64_t *value);
int chris_seg_load_cs(ChrisCpu *cpu, uint16_t sel);
int chris_execute(ChrisCpu *cpu, const ChrisInsn *in);
uint64_t chris_imm_sx(const ChrisInsn *in);
int chris_read_gpr(const ChrisCpu *cpu, const ChrisInsn *in, int reg, int os, uint64_t *out);
int chris_write_gpr(ChrisCpu *cpu, const ChrisInsn *in, int reg, int os, uint64_t value);
int chris_eff_addr(const ChrisCpu *cpu, const ChrisInsn *in, uint64_t *ea);
int chris_read_rm(ChrisCpu *cpu, const ChrisInsn *in, uint64_t *out);
int chris_write_rm(ChrisCpu *cpu, const ChrisInsn *in, uint64_t value);
int chris_read_regop(const ChrisCpu *cpu, const ChrisInsn *in, uint64_t *out);
int chris_write_regop(ChrisCpu *cpu, const ChrisInsn *in, uint64_t value);

void chris_log(ChrisMachine *m, const char *line);
void chris_trace_push(ChrisCpu *cpu, uint64_t rip, const uint8_t *bytes, int len, const char *text);
void chris_trace_dump(const ChrisCpu *cpu);

extern const ChrisCpuBackend chriscpu_backend;
extern const ChrisCpuBackend chrishv_backend;

#endif
