#include "machine/machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_fail;

static void check(int cond, const char *msg) {
    if (!cond) {
        fprintf(stderr, "FAIL %s\n", msg);
        g_fail++;
    }
}

static ChrisMachine *machine_with(const uint8_t *code, size_t n, uint64_t steps) {
    ChrisConfig cfg;
    ChrisMachine *m;
    chris_config_init(&cfg);
    cfg.max_steps = steps;
    m = chris_machine_create(&cfg);
    if (!m) {
        return 0;
    }
    if (code && n) {
        if (chris_write_ram(m, 0x1000, code, n) != 0) {
            chris_machine_destroy(m);
            return 0;
        }
    }
    if (chris_boot(m, 0x1000, CHRIS_STACK_RSP) != 0) {
        chris_machine_destroy(m);
        return 0;
    }
    return m;
}

static int run_code(const uint8_t *code, size_t n, ChrisMachine **out) {
    ChrisMachine *m = machine_with(code, n, 10000);
    int reason;
    if (!m) {
        return -1;
    }
    reason = chris_run(m, 10000);
    *out = m;
    return reason;
}

static void test_flags(void) {
    uint64_t r = 0;
    uint64_t f = chris_flags_bin(CHRIS_ALU_ADD, ~0ull, 1, 8, 2, &r);
    check(r == 0, "add overflow result");
    check((f & 1ull) != 0, "add CF");
    check((f & (1ull << 6)) != 0, "add ZF");
    check((f & (1ull << 7)) == 0, "add SF");
    check((f & (1ull << 11)) == 0, "add OF");
    check((f & (1ull << 2)) != 0, "add PF");
    check((f & (1ull << 4)) != 0, "add AF");
    f = chris_flags_bin(CHRIS_ALU_ADD, 0x7fffffffffffffffull, 1, 8, 2, &r);
    check(r == 0x8000000000000000ull, "signed overflow result");
    check((f & (1ull << 11)) != 0, "signed OF");
    check((f & 1ull) == 0, "signed CF clear");
}

static void test_cpu_add(void) {
    static const uint8_t code[] = {
        0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff, 0x48, 0x83, 0xc0, 0x01, 0xf4,
    };
    ChrisMachine *m = 0;
    int reason = run_code(code, sizeof code, &m);
    check(reason == CHRIS_EXIT_HLT, "add halt");
    check(m && chris_get_gpr(m, 0) == 0, "add rax");
    check(m && (chris_get_rflags(m) & 1ull) != 0, "cpu CF");
    check(m && (chris_get_rflags(m) & (1ull << 6)) != 0, "cpu ZF");
    chris_machine_destroy(m);
}

static void test_mem_call(void) {
    static const uint8_t code[] = {
        0x48, 0xc7, 0xc0, 0x0a, 0x00, 0x00, 0x00, 0x48, 0xc7, 0xc3, 0x14, 0x00, 0x00, 0x00,
        0x48, 0x01, 0xd8, 0x48, 0xc7, 0xc7, 0x00, 0x40, 0x00, 0x00, 0x48, 0x89, 0x07, 0x48,
        0x8b, 0x0f, 0xe8, 0x01, 0x00, 0x00, 0x00, 0xf4, 0x48, 0xff, 0xc0, 0xc3,
    };
    ChrisMachine *m = 0;
    uint64_t mem = 0;
    int reason = run_code(code, sizeof code, &m);
    check(reason == CHRIS_EXIT_HLT, "mem halt");
    check(m && chris_get_gpr(m, 0) == 31, "call result");
    if (m) {
        chris_read_ram(m, 0x4000, &mem, 8);
    }
    check(mem == 30, "stored qword");
    chris_machine_destroy(m);
}

static void test_serial_and_ports(void) {
    static const uint8_t loopb[] = {
        0x66, 0xba, 0xfc, 0x03, 0xb0, 0x10, 0xee, 0x66, 0xba, 0xf8, 0x03, 0xb0, 0xae, 0xee, 0xec, 0xf4,
    };
    static const uint8_t unass[] = {0x66, 0xba, 0x80, 0x00, 0xec, 0xf4};
    static const uint8_t shut[] = {0x66, 0xba, 0x01, 0x05, 0xb0, 0x01, 0xee, 0xf4};
    ChrisMachine *m = 0;
    check(run_code(loopb, sizeof loopb, &m) == CHRIS_EXIT_HLT, "loop halt");
    check(m && (chris_get_gpr(m, 0) & 0xffull) == 0xae, "loopback byte");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(unass, sizeof unass, &m) == CHRIS_EXIT_HLT, "unassigned halt");
    check(m && (chris_get_gpr(m, 0) & 0xffull) == 0xff, "unassigned in");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(shut, sizeof shut, &m) == CHRIS_EXIT_SHUTDOWN, "shutdown port");
    chris_machine_destroy(m);
}

static void test_faults(void) {
    static const uint8_t ud[] = {0x0f, 0x0b, 0xf4};
    static const uint8_t pf[] = {0x48, 0xc7, 0xc0, 0x00, 0x00, 0x00, 0x01, 0x48, 0x8b, 0x18, 0xf4};
    static const uint8_t gp[] = {0x48, 0xb8, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80, 0x00,
                                 0x48, 0x8b, 0x18, 0xf4};
    static const uint8_t spin[] = {0xeb, 0xfe};
    static const uint8_t div0[] = {0x48, 0x31, 0xc0, 0x48, 0x31, 0xd2, 0x48, 0xf7, 0xf0, 0xf4};
    ChrisMachine *m = 0;
    check(run_code(ud, sizeof ud, &m) == CHRIS_EXIT_EXCEPTION, "ud exit");
    check(m && chris_exception_vector(m) == CHRIS_EX_UD, "ud vector");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(pf, sizeof pf, &m) == CHRIS_EXIT_EXCEPTION, "pf exit");
    check(m && chris_exception_vector(m) == CHRIS_EX_PF, "pf vector");
    check(m && chris_get_cr(m, 2) == 0x01000000ull, "cr2");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(gp, sizeof gp, &m) == CHRIS_EXIT_EXCEPTION, "gp exit");
    check(m && chris_exception_vector(m) == CHRIS_EX_GP, "gp vector");
    chris_machine_destroy(m);
    m = machine_with(spin, sizeof spin, 32);
    check(m && chris_run(m, 32) == CHRIS_EXIT_STEP_LIMIT, "step limit");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(div0, sizeof div0, &m) == CHRIS_EXIT_EXCEPTION, "div0");
    check(m && chris_exception_vector(m) == CHRIS_EX_DE, "de vector");
    chris_machine_destroy(m);
}

static void test_cpuid_msr(void) {
    static const uint8_t id[] = {0xb8, 0x00, 0x00, 0x00, 0x00, 0x0f, 0xa2, 0xf4};
    static const uint8_t msr[] = {0xb9, 0x80, 0x00, 0x00, 0xc0, 0xba, 0x00, 0x00, 0x00, 0x00,
                                  0xb8, 0x00, 0x01, 0x00, 0x00, 0x0f, 0x30, 0x0f, 0x32, 0xf4};
    ChrisMachine *m = 0;
    uint32_t a, b, c, d;
    chris_cpuid(0, 0, &a, &b, &c, &d);
    check(a == 1, "cpuid max");
    check(b == 0x69726843u && d == 0x55504373u && c == 0x20202020u, "vendor ChrisCPU");
    check(run_code(id, sizeof id, &m) == CHRIS_EXIT_HLT, "cpuid halt");
    check(m && chris_get_gpr(m, 3) == 0x69726843u, "cpuid ebx");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(msr, sizeof msr, &m) == CHRIS_EXIT_HLT, "msr halt");
    check(m && chris_get_gpr(m, 0) == 0x100, "efer readback");
    chris_machine_destroy(m);
}

static void test_elf_and_reject(void) {
    FILE *f = fopen(CHRIS_GUEST, "rb");
    uint8_t *buf;
    long sz;
    ChrisConfig cfg;
    ChrisMachine *m;
    char err[128];
    uint8_t bad[8] = {0, 1, 2, 3, 4, 5, 6, 7};
    uint8_t hi[sizeof(uint64_t) * 8];
    check(f != 0, "open guest");
    if (!f) {
        return;
    }
    fseek(f, 0, SEEK_END);
    sz = ftell(f);
    rewind(f);
    buf = (uint8_t *)malloc((size_t)sz);
    check(buf && fread(buf, 1, (size_t)sz, f) == (size_t)sz, "read guest");
    fclose(f);
    chris_config_init(&cfg);
    m = chris_machine_create(&cfg);
    err[0] = 0;
    check(chris_load_elf(m, buf, (size_t)sz, err, sizeof err) == 0, "load guest elf");
    check(chris_boot(m, 0, CHRIS_STACK_RSP) == 0, "boot guest");
    check(chris_run(m, 100000) == CHRIS_EXIT_HLT, "guest halt");
    check(chris_get_gpr(m, 0) == 31, "guest rax");
    check(strcmp(chris_serial_text(m, 0), "OK\n") == 0, "guest serial");
    chris_machine_destroy(m);
    m = chris_machine_create(&cfg);
    check(chris_load_elf(m, bad, sizeof bad, err, sizeof err) != 0, "bad magic");
    chris_machine_destroy(m);
    m = chris_machine_create(&cfg);
    memset(hi, 0, sizeof hi);
    hi[0] = 0x7f;
    hi[1] = 'E';
    hi[2] = 'L';
    hi[3] = 'F';
    hi[4] = 2;
    hi[5] = 1;
    hi[18] = 62;
    check(m && chris_load_elf(m, hi, sizeof hi, err, sizeof err) != 0, "truncated elf");
    chris_machine_destroy(m);
    free(buf);
    cfg.backend = "chrishv";
    check(chris_machine_create(&cfg) == 0, "chrishv refused");
}

static void test_fuzz_decode(void) {
    uint32_t s = 1;
    int i;
    for (i = 0; i < 2000; ++i) {
        uint8_t b[15];
        ChrisInsn insn;
        int n;
        int j;
        int rc;
        s = s * 1664525u + 1013904223u;
        n = 1 + (int)(s % 15u);
        for (j = 0; j < n; ++j) {
            s = s * 1664525u + 1013904223u;
            b[j] = (uint8_t)(s >> 16);
        }
        rc = chris_decode(b, n, &insn);
        check(rc == -1 || (rc > 0 && rc <= 15 && insn.len == rc), "decode bounds");
        if (g_fail > 20) {
            break;
        }
    }
}

static int mmio_read(void *ctx, uint64_t offset, int size, uint64_t *value) {
    uint64_t *cell = (uint64_t *)ctx;
    (void)size;
    if (offset >= 8) {
        return -1;
    }
    *value = (*cell >> (offset * 8ull)) & 0xffull;
    return 0;
}

static int mmio_write(void *ctx, uint64_t offset, int size, uint64_t value) {
    uint64_t *cell = (uint64_t *)ctx;
    uint64_t shift;
    (void)size;
    if (offset >= 8) {
        return -1;
    }
    shift = offset * 8ull;
    *cell = (*cell & ~(0xffull << shift)) | ((value & 0xffull) << shift);
    return 0;
}

static void test_mmio_real(void) {
    /* 32 MiB sits in PDPT[0], above the 16 MiB identity RAM map. */
    static const uint8_t code[] = {0x48, 0xb8, 0x00, 0x00, 0x00, 0x02, 0x00, 0x00, 0x00, 0x00,
                                   0x48, 0xc7, 0x00, 0x2a, 0x00, 0x00, 0x00, 0x48, 0x8b, 0x00, 0xf4};
    uint64_t cell = 0;
    ChrisConfig cfg;
    ChrisMachine *m;
    chris_config_init(&cfg);
    m = chris_machine_create(&cfg);
    check(chris_mmio_map(m, 0x02000000ull, 0x1000ull, mmio_read, mmio_write, &cell) == 0, "mmio map");
    check(chris_write_ram(m, 0x1000, code, sizeof code) == 0, "mmio bytes");
    check(chris_boot(m, 0x1000, CHRIS_STACK_RSP) == 0, "mmio boot2");
    {
        uint64_t pde = 0x02000000ull | 0x83ull;
        uint64_t pd = cfg.ram_size - 0x2000ull;
        uint64_t index = (0x02000000ull >> 21) & 0x1ffull;
        check(chris_write_ram(m, pd + index * 8ull, &pde, 8) == 0, "mmio pde");
    }
    check(chris_run(m, 1000) == CHRIS_EXIT_HLT, "mmio halt");
    check(cell == 0x2a, "mmio stored");
    check(chris_get_gpr(m, 0) == 0x2a, "mmio loaded");
    chris_machine_destroy(m);
}

static void test_div_mul_msr(void) {
    static const uint8_t idiv8[] = {0x66, 0xb8, 0xf8, 0xff, 0xb3, 0x02, 0xf6, 0xfb, 0xf4};
    static const uint8_t divov[] = {0x66, 0xb8, 0x00, 0x01, 0xb3, 0x01, 0xf6, 0xf3, 0xf4};
    static const uint8_t mul[] = {0x48, 0xc7, 0xc0, 0xff, 0xff, 0xff, 0xff, 0x48, 0xc7, 0xc3, 0x02,
                                  0x00, 0x00, 0x00, 0x48, 0xf7, 0xe3, 0xf4};
    static const uint8_t badmsr[] = {0xb9, 0x23, 0x01, 0x00, 0x00, 0x0f, 0x32, 0xf4};
    static const uint8_t miss[] = {0x48, 0xb8, 0x00, 0x00, 0x00, 0x04, 0x00, 0x00, 0x00, 0x00, 0x48,
                                   0x8b, 0x18, 0xf4};
    static const uint8_t push[] = {0x50, 0xf4};
    ChrisMachine *m = 0;
    ChrisConfig cfg;
    check(run_code(idiv8, sizeof idiv8, &m) == CHRIS_EXIT_HLT, "idiv8 halt");
    check(m && (chris_get_gpr(m, 0) & 0xffffull) == 0x00fcull, "idiv8 quotient");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(divov, sizeof divov, &m) == CHRIS_EXIT_EXCEPTION, "div overflow");
    check(m && chris_exception_vector(m) == CHRIS_EX_DE, "div overflow vector");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(mul, sizeof mul, &m) == CHRIS_EXIT_HLT, "mul halt");
    check(m && chris_get_gpr(m, 0) == 0xfffffffffffffffeull, "mul low");
    check(m && chris_get_gpr(m, 2) == 1, "mul high");
    check(m && (chris_get_rflags(m) & 1ull) != 0, "mul CF");
    chris_machine_destroy(m);
    m = 0;
    check(run_code(badmsr, sizeof badmsr, &m) == CHRIS_EXIT_EXCEPTION, "bad msr");
    check(m && chris_exception_vector(m) == CHRIS_EX_GP, "bad msr gp");
    chris_machine_destroy(m);
    chris_config_init(&cfg);
    m = chris_machine_create(&cfg);
    check(chris_write_ram(m, 0x1000, miss, sizeof miss) == 0, "unmapped bytes");
    check(chris_boot(m, 0x1000, CHRIS_STACK_RSP) == 0, "unmapped boot");
    {
        uint64_t pde = 0x04000000ull | 0x83ull;
        uint64_t index = (0x04000000ull >> 21) & 0x1ffull;
        check(chris_write_ram(m, cfg.ram_size - 0x2000ull + index * 8ull, &pde, 8) == 0, "unmapped pde");
    }
    check(chris_run(m, 100) == CHRIS_EXIT_UNMAPPED, "unmapped mmio");
    chris_machine_destroy(m);
    m = machine_with(push, sizeof push, 10);
    if (m) {
        chris_set_gpr(m, CHRIS_GPR_RSP, 0x01000008ull);
    }
    check(m && chris_run(m, 10) == CHRIS_EXIT_EXCEPTION, "stack push fault");
    check(m && chris_exception_vector(m) == CHRIS_EX_PF, "stack push pf");
    check(m && chris_get_cr(m, 2) == 0x01000000ull, "stack cr2");
    chris_machine_destroy(m);
}

int main(void) {
    test_flags();
    test_cpu_add();
    test_mem_call();
    test_serial_and_ports();
    test_faults();
    test_cpuid_msr();
    test_div_mul_msr();
    test_elf_and_reject();
    test_fuzz_decode();
    test_mmio_real();
    if (g_fail) {
        fprintf(stderr, "chrisvm tests failed: %d\n", g_fail);
        return 1;
    }
    printf("chrisvm tests ok\n");
    return 0;
}
