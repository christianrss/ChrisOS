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
    {
        static const char vol[] =
            "uint32_t port;\n"
            "volatile uint32_t mmio;\n"
            "void plain(void) {\n"
            "    port = 1;\n"
            "    port = 2;\n"
            "}\n"
            "void poke(void) {\n"
            "    mmio = 1;\n"
            "    mmio = 2;\n"
            "}\n"
            "uint32_t peek(void) {\n"
            "    uint32_t a;\n"
            "    uint32_t b;\n"
            "    a = mmio;\n"
            "    b = mmio;\n"
            "    return a + b;\n"
            "}\n";
        const char *as;
        int port_stores;
        int mmio_stores;
        int mmio_loads;
        const char *p;
        if (kcc_compile_named("volatile.c", vol, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "volatile failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            return 1;
        }
        as = kcc_last_asm();
        port_stores = 0;
        mmio_stores = 0;
        mmio_loads = 0;
        p = as;
        while (p && (p = strstr(p, "mov [rel port], rax")) != 0) {
            port_stores++;
            p += 18;
        }
        p = as;
        while (p && (p = strstr(p, "mov dword [rel mmio], eax")) != 0) {
            mmio_stores++;
            p += 26;
        }
        p = as;
        while (p && (p = strstr(p, "mov eax, dword [rel mmio]")) != 0) {
            mmio_loads++;
            p += 26;
        }
        if (port_stores != 1 || mmio_stores != 2 || mmio_loads != 2) {
            fprintf(stderr, "volatile asm port=%d mmio_st=%d mmio_ld=%d\n%s\n",
                    port_stores, mmio_stores, mmio_loads, as);
            return 1;
        }
    }
    {
        static const char *need[] = {
            "memset", "memcpy", "memcmp", "strlen", "strncpy", "strcmp", "strncmp"
        };
        unsigned i;
        char *ssrc = read_file("kernel/metal/string.c");
        if (!ssrc) {
            fprintf(stderr, "cannot read string.c\n");
            return 1;
        }
        if (kcc_compile_named("kernel/metal/string.c", ssrc, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "string.c failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            free(ssrc);
            return 1;
        }
        free(ssrc);
        for (i = 0; i < sizeof(need) / sizeof(need[0]); i++) {
            if (!find_sym(&img, need[i])) {
                fprintf(stderr, "string.c missing %s\n", need[i]);
                return 1;
            }
        }
    }
    {
        static const char layout[] =
            "typedef struct Pair {\n"
            "    uint8_t a;\n"
            "    uint32_t b;\n"
            "} Pair;\n"
            "typedef struct __attribute__((packed)) Tight {\n"
            "    uint8_t a;\n"
            "    uint32_t b;\n"
            "} Tight;\n"
            "uint32_t getb(Pair *p) { return p->b; }\n"
            "uint32_t gett(Tight *p) { return p->b; }\n";
        const char *as;
        if (kcc_compile_named("layout.c", layout, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "layout failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            return 1;
        }
        as = kcc_last_asm();
        if (!strstr(as, "getb:") || !strstr(as, "mov rcx, 4") || !strstr(as, "mov rcx, 1")) {
            fprintf(stderr, "struct layout asm:\n%s\n", as);
            return 1;
        }
    }
    {
        char *msrc = read_file("kernel/metal/meminfo.c");
        char *psrc = read_file("kernel/metal/pit.c");
        if (!msrc || !psrc) {
            fprintf(stderr, "cannot read meminfo.c or pit.c\n");
            free(msrc);
            free(psrc);
            return 1;
        }
        if (kcc_compile_named("kernel/metal/meminfo.c", msrc, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "meminfo.c failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            free(msrc);
            free(psrc);
            return 1;
        }
        if (!find_sym(&img, "mem_format")) {
            fprintf(stderr, "meminfo.c missing mem_format\n");
            free(msrc);
            free(psrc);
            return 1;
        }
        if (kcc_compile_named("kernel/metal/pit.c", psrc, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "pit.c failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            free(msrc);
            free(psrc);
            return 1;
        }
        free(msrc);
        free(psrc);
    }
    puts("test_kcc: ok");
    return 0;
}
