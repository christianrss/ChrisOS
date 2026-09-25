#include "kcc.h"
#include "chrisasm.h"
#include "chrisld.h"
#include "chriso.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static char *read_file(const char *path) {
    FILE *f = fopen(path, "rb");
    long sz;
    char *buf;

    if (!f) {
        return 0;
    }
    if (fseek(f, 0, SEEK_END) != 0) {
        fclose(f);
        return 0;
    }
    sz = ftell(f);
    if (sz < 0) {
        fclose(f);
        return 0;
    }
    if (fseek(f, 0, SEEK_SET) != 0) {
        fclose(f);
        return 0;
    }
    buf = (char *)malloc((size_t)sz + 1u);
    if (!buf) {
        fclose(f);
        return 0;
    }
    if (fread(buf, 1, (size_t)sz, f) != (size_t)sz) {
        free(buf);
        fclose(f);
        return 0;
    }
    buf[sz] = 0;
    fclose(f);
    return buf;
}

static int find_sym(const ChrisoImage *img, const char *name) {
    uint32_t i;
    for (i = 0; i < img->nsym; i++) {
        if (strcmp(img->sym[i].name, name) == 0) {
            return 1;
        }
    }
    return 0;
}

int main(void) {
    ChrisoImage img;
    const KccDiag *diag;
    char *src = read_file("tools/kcc_fixtures/level0.c");
    const char *serial_path = "kernel/metal/serial.c";

    if (!src) {
        fprintf(stderr, "cannot read level0 fixture\n");
        return 1;
    }
    if (kcc_compile_named("tools/kcc_fixtures/level0.c", src, &img) != 0) {
        diag = kcc_last_error();
        fprintf(stderr, "level0 failed: %s:%d:%d: %s\n", diag->file, diag->line,
                diag->column, diag->message);
        free(src);
        return 1;
    }
    free(src);
    diag = kcc_last_error();
    if (diag->severity != 0 || img.sec_size[CHRISO_SEC_TEXT] == 0u) {
        fprintf(stderr, "level0 produced no text\n");
        return 1;
    }
    if (!find_sym(&img, "kstart") || !find_sym(&img, "outb")) {
        fprintf(stderr, "level0 missing kstart or outb symbol\n");
        return 1;
    }
    {
        uint32_t i;
        int outb_undef = 0;
        for (i = 0; i < img.nsym; i++) {
            if (strcmp(img.sym[i].name, "outb") == 0 &&
                img.sym[i].binding == CHRISO_BIND_UNDEF) {
                outb_undef = 1;
            }
        }
        if (!outb_undef || img.nrel < 2u || img.rel[0].type != R_X86_64_PLT32) {
            fprintf(stderr, "level0 call was not a relocation\n");
            return 1;
        }
    }
    src = read_file(serial_path);
    if (!src) {
        fprintf(stderr, "cannot read serial.c\n");
        return 1;
    }
    if (kcc_compile_named(serial_path, src, &img) != 0) {
        diag = kcc_last_error();
        fprintf(stderr, "serial.c failed: %s:%d:%d: %s\n", diag->file, diag->line,
                diag->column, diag->message);
        free(src);
        return 1;
    }
    free(src);
    {
        static const char *need[] = {
            "serial_init", "serial_putc", "serial_puts",
            "serial_write_hex", "serial_write_u64",
            "serial_available", "g_serial_lock"
        };
        uint32_t i;
        int saw_avail = 0;
        int saw_lock = 0;
        for (i = 0; i < sizeof(need) / sizeof(need[0]); i++) {
            if (!find_sym(&img, need[i])) {
                fprintf(stderr, "serial.c missing %s\n", need[i]);
                return 1;
            }
        }
        if (img.sec_size[CHRISO_SEC_RODATA] == 0u) {
            fprintf(stderr, "serial.c produced no rodata\n");
            return 1;
        }
        for (i = 0; i < img.nsym; i++) {
            if (strcmp(img.sym[i].name, "serial_available") == 0 &&
                img.sym[i].section == CHRISO_SEC_BSS) {
                saw_avail = 1;
            }
            if (strcmp(img.sym[i].name, "g_serial_lock") == 0 &&
                img.sym[i].section == CHRISO_SEC_BSS) {
                saw_lock = 1;
            }
        }
        if (!saw_avail || !saw_lock) {
            fprintf(stderr, "serial.c bss objects missing\n");
            return 1;
        }
        {
            ChrisoImage klog;
            ChrisoImage stubs;
            const ChrisoImage *objs[3];
            uint8_t elf[65536];
            uint64_t entry;
            int n;
            int saw_ring = 0;
            static const char *kneed[] = {
                "klog_init", "klog_putc", "klog_puts", "klog_copy"
            };
            char *ksrc = read_file("kernel/metal/klog.c");
            if (!ksrc) {
                fprintf(stderr, "cannot read klog.c\n");
                return 1;
            }
            if (kcc_compile_named("kernel/metal/klog.c", ksrc, &klog) != 0) {
                diag = kcc_last_error();
                fprintf(stderr, "klog.c failed: %s:%d:%d: %s\n", diag->file,
                        diag->line, diag->column, diag->message);
                free(ksrc);
                return 1;
            }
            free(ksrc);
            for (i = 0; i < sizeof(kneed) / sizeof(kneed[0]); i++) {
                if (!find_sym(&klog, kneed[i])) {
                    fprintf(stderr, "klog.c missing %s\n", kneed[i]);
                    return 1;
                }
            }
            if (klog.sec_size[CHRISO_SEC_BSS] < 8192u) {
                fprintf(stderr, "klog.c bss is %u\n", klog.sec_size[CHRISO_SEC_BSS]);
                return 1;
            }
            for (i = 0; i < klog.nsym; i++) {
                if (strcmp(klog.sym[i].name, "g_log") == 0 &&
                    klog.sym[i].section == CHRISO_SEC_BSS) {
                    saw_ring = 1;
                }
            }
            if (!saw_ring) {
                fprintf(stderr, "klog.c ring is not a bss object\n");
                return 1;
            }
            if (chrisasm_assemble(
                    "inb:\nmov rax, 0\nret\n"
                    "outb:\nret\n"
                    "spin_init:\nret\n"
                    "spin_lock:\nret\n"
                    "spin_unlock:\nret\n",
                    &stubs) != 0) {
                fprintf(stderr, "stub assemble failed\n");
                return 1;
            }
            objs[0] = &img;
            objs[1] = &klog;
            objs[2] = &stubs;
            n = chrisld_link_objects(objs, 3u, 0x400000ull, elf, sizeof(elf), &entry);
            if (n < 64 || chrisld_validate(elf, (uint32_t)n) != 0 || elf[56] != 2) {
                fprintf(stderr, "serial link failed phnum=%u n=%d\n", elf[56], n);
                return 1;
            }
        }
    }
    puts("test_kcc: ok");
    return 0;
}
