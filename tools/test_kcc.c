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

static int text_has(const ChrisoImage *img, const uint8_t *pat, uint32_t n) {
    uint32_t i;
    if (!img->sec[CHRISO_SEC_TEXT] || img->sec_size[CHRISO_SEC_TEXT] < n) {
        return 0;
    }
    for (i = 0; i + n <= img->sec_size[CHRISO_SEC_TEXT]; i++) {
        if (memcmp(img->sec[CHRISO_SEC_TEXT] + i, pat, n) == 0) {
            return 1;
        }
    }
    return 0;
}

static int compile_must(const char *path, const char *sym, ChrisoImage *img) {
    char *src = read_file(path);
    const KccDiag *diag;
    if (!src) {
        fprintf(stderr, "cannot read %s\n", path);
        return 1;
    }
    if (kcc_compile_named(path, src, img) != 0) {
        diag = kcc_last_error();
        fprintf(stderr, "%s failed: %s:%d:%d: %s\n", path, diag->file, diag->line,
                diag->column, diag->message);
        free(src);
        return 1;
    }
    free(src);
    if (sym && !find_sym(img, sym)) {
        fprintf(stderr, "%s missing %s\n", path, sym);
        return 1;
    }
    return 0;
}

static int compile_fails(const char *name, const char *src, const char *needle) {
    ChrisoImage img;
    const KccDiag *diag;
    if (kcc_compile_named(name, src, &img) == 0) {
        fprintf(stderr, "%s compiled but must fail\n", name);
        return 1;
    }
    diag = kcc_last_error();
    if (!diag->message[0] || !strstr(diag->message, needle)) {
        fprintf(stderr, "%s diagnostic %s, want %s\n", name, diag->message, needle);
        return 1;
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
    {
        static const char *paths[] = {
            "kernel/metal/acpi.c", "kernel/metal/apic.c", "kernel/metal/elf.c",
            "kernel/metal/heap.c", "kernel/metal/ioapic.c", "kernel/metal/job.c",
            "kernel/metal/bootinfo.c", "kernel/metal/buildid.c", "kernel/metal/gdt.c",
            "kernel/metal/idt.c", "kernel/metal/irq.c", "kernel/metal/kcc_job.c",
            "kernel/metal/kthread.c", "kernel/metal/mm.c", "kernel/metal/panic.c",
            "kernel/metal/pci.c", "kernel/metal/pmm.c", "kernel/metal/port.c",
            "kernel/metal/proc.c", "kernel/metal/ps2.c", "kernel/metal/smp.c",
            "kernel/metal/spin.c",             "kernel/metal/syscall.c", "kernel/metal/tlb_proto.c",
            "kernel/metal/user_enter.c", "kernel/metal/start.c"
        };
        static const char *syms[] = {
            "acpi_probe", "apic_ipi_nmi", "elf_load", "kmalloc", "ioapic_init",
            "job_worker_forever", "bootinfo_init", "build_info_log", "gdt_init",
            "idt_load", "pic_init", "kcc_job_submit_path", "kthread_create", "mm_init",
            "panic", "pci_read", "pmm_alloc", "inb", "proc_init", "ps2_init", "smp_init",
            "spin_lock", "syscall_init", "tlb_runtime_init", "enter_user", "kstart"
        };
        unsigned i;
        for (i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
            if (compile_must(paths[i], syms[i], &img) != 0) {
                return 1;
            }
            if (strcmp(syms[i], "bootinfo_init") == 0) {
                static const uint8_t marker[] = {
                    0xae, 0xd1, 0xe7, 0x9d, 0xb3, 0xf4, 0xb8, 0xf6
                };
                if (!img.sec[CHRISO_SEC_DATA] ||
                    img.sec_size[CHRISO_SEC_DATA] < sizeof(marker) ||
                    memcmp(img.sec[CHRISO_SEC_DATA], marker, sizeof(marker)) != 0) {
                    fprintf(stderr, "bootinfo.c limine marker is not initialized data\n");
                    return 1;
                }
            }
            if (strcmp(syms[i], "inb") == 0) {
                static const uint8_t in_al[] = {0xec};
                static const uint8_t out_al[] = {0xee};
                static const uint8_t in_ax[] = {0x66, 0xed};
                static const uint8_t out_ax[] = {0x66, 0xef};
                static const uint8_t in_eax[] = {0xed};
                static const uint8_t out_eax[] = {0xef};
                static const uint8_t hlt[] = {0xf4};
                if (!text_has(&img, in_al, 1) || !text_has(&img, out_al, 1) ||
                    !text_has(&img, in_ax, 2) || !text_has(&img, out_ax, 2) ||
                    !text_has(&img, in_eax, 1) || !text_has(&img, out_eax, 1) ||
                    !text_has(&img, hlt, 1)) {
                    fprintf(stderr, "port.c missing in/out/hlt bytes\n");
                    return 1;
                }
            }
            if (strcmp(syms[i], "gdt_init") == 0) {
                static const uint8_t lgdt[] = {0x0f, 0x01, 0x10};
                static const uint8_t push8[] = {0x6a, 0x08};
                static const uint8_t lretq[] = {0x48, 0xcb};
                static const uint8_t movax[] = {0x66, 0xb8, 0x10, 0x00};
                static const uint8_t strax[] = {0x66, 0x0f, 0x00, 0xc8};
                if (!text_has(&img, lgdt, 3) || !text_has(&img, push8, 2) ||
                    !text_has(&img, lretq, 2) || !text_has(&img, movax, 4) ||
                    !text_has(&img, strax, 4)) {
                    fprintf(stderr, "gdt.c missing lgdt/lretq/str bytes\n");
                    return 1;
                }
            }
            if (strcmp(syms[i], "spin_lock") == 0) {
                static const uint8_t cas[] = {0xf0, 0x0f, 0xb1, 0x11};
                if (!text_has(&img, cas, 4)) {
                    fprintf(stderr, "spin.c missing lock cmpxchg\n");
                    return 1;
                }
            }
            if (strcmp(syms[i], "enter_user") == 0) {
                static const uint8_t iretq[] = {0x48, 0xcf};
                if (!text_has(&img, iretq, 2)) {
                    fprintf(stderr, "user_enter.c missing iretq\n");
                    return 1;
                }
            }
        }
    }
    {
        static const char src[] =
            "int acc(int n) {\n"
            "    int i;\n"
            "    int s;\n"
            "    s = 0;\n"
            "    for (i = 0; i < n; i = i + 1) {\n"
            "        if (i == 1) continue;\n"
            "        if (i == 4) break;\n"
            "        s = s + 1;\n"
            "    }\n"
            "    return s;\n"
            "}\n"
            "int eq(int i) { return i == 1; }\n"
            "uint32_t pack(uint32_t bus) { return bus << 16; }\n"
            "uint64_t bits(uint64_t v) { return ~v; }\n"
            "uint64_t sz(void) { return sizeof(uint32_t); }\n"
            "_Static_assert(sizeof(uint32_t) == 4, \"u32\");\n"
            "_Static_assert((~0ull & 1ull) == 1ull, \"lowbit\");\n"
            "void halt(void) { __asm__ volatile (\"hlt\"); }\n"
            "void pause_once(void) { __asm__ volatile (\"pause\"); }\n"
            "void irqs(void) { __asm__ volatile (\"cli\"); __asm__ volatile (\"sti\"); }\n";
        const char *as;
        const char *add;
        const char *back;
        static const uint8_t not_rax[] = {0x48, 0xf7, 0xd0};
        static const uint8_t hlt[] = {0xf4};
        static const uint8_t pause[] = {0xf3, 0x90};
        static const uint8_t cli[] = {0xfa};
        static const uint8_t sti[] = {0xfb};
        if (kcc_compile_named("subset.c", src, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "subset failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            return 1;
        }
        as = kcc_last_asm();
        /* continue targets the step, which adds before jumping to the head. */
        {
            int ok = 0;
            const char *p = as;
            while ((p = strstr(p, "jmp .L")) != 0) {
                char lab[16];
                int n = 0;
                const char *q = p + 4;
                const char *at;
                while (*q && *q != '\n' && n < 15) {
                    lab[n++] = *q++;
                }
                lab[n] = 0;
                at = q;
                while ((at = strstr(at, lab)) != 0 && at[n] != ':') {
                    at += n;
                }
                if (at && at[n] == ':') {
                    add = strstr(at, "\nadd ");
                    back = strstr(at, "\njmp ");
                    if (add && back && add < back) {
                        ok = 1;
                        break;
                    }
                }
                p += 4;
            }
            if (!ok) {
                fprintf(stderr, "continue does not reach the for step\n%s\n", as);
                return 1;
            }
        }
        if (!strstr(as, "mov rax, 1\nmov rcx, rax\n") ||
            !strstr(as, "mov rax, 16\nmov rcx, rax\n") || !strstr(as, "not rax") ||
            !strstr(as, "mov rax, 4\n")) {
            fprintf(stderr, "literal or sizeof asm:\n%s\n", as);
            return 1;
        }
        if (!text_has(&img, not_rax, 3) || !text_has(&img, hlt, 1) ||
            !text_has(&img, pause, 2) || !text_has(&img, cli, 1) || !text_has(&img, sti, 1)) {
            fprintf(stderr, "subset missing not/hlt/pause/cli/sti bytes\n");
            return 1;
        }
    }
    {
        static const char src[] =
            "#define PASTE(A, B) A##B\n"
            "#define ID(X) X\n"
            "int PASTE(fo, o)(void) { return ID(7); }\n"
            "#define REV 3\n"
            "#if REV > 3\n"
            "#error too new\n"
            "#elif defined(REV) && REV >= 3\n"
            "int gate(void) { return 9; }\n"
            "#else\n"
            "int gate(void) { return 0; }\n"
            "#endif\n"
            "#if 0\n"
            "#error hidden\n"
            "#endif\n";
        const char *as;
        if (kcc_compile_named("pp.c", src, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "pp failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            return 1;
        }
        as = kcc_last_asm();
        if (!find_sym(&img, "foo") || !find_sym(&img, "gate") ||
            !strstr(as, "mov rax, 7\n") || !strstr(as, "mov rax, 9\n")) {
            fprintf(stderr, "preprocessor asm:\n%s\n", as);
            return 1;
        }
    }
    {
        static const char src[] =
            "typedef struct Inner {\n"
            "    uint32_t a;\n"
            "    uint16_t b;\n"
            "} Inner;\n"
            "typedef struct Outer {\n"
            "    uint64_t id[2];\n"
            "    Inner in;\n"
            "    uint64_t rev;\n"
            "} Outer;\n"
            "static Outer req = { .id = { 1, 2 }, .rev = 3 };\n"
            "typedef enum { KIND_A = 1, KIND_B } Kind;\n"
            "int pick(int x) {\n"
            "    switch (x) {\n"
            "    case KIND_A: return 10;\n"
            "    case KIND_B: return 20;\n"
            "    default: return 0;\n"
            "    }\n"
            "}\n"
            "int hop(int n) {\n"
            "    if (n == 0) goto done;\n"
            "    n = 4;\n"
            "done:\n"
            "    return n;\n"
            "}\n"
            "extern unsigned char stack_top[];\n"
            "extern void (*stubs[4])(void);\n"
            "char tag[] = \"KCC\";\n"
            "uint64_t top_addr(void) { return (uint64_t)stack_top; }\n";
        static const uint8_t magic[] = {
            1, 0, 0, 0, 0, 0, 0, 0, 2, 0, 0, 0, 0, 0, 0, 0,
            0, 0, 0, 0, 0, 0, 0, 0, 3, 0, 0, 0, 0, 0, 0, 0
        };
        if (kcc_compile_named("init.c", src, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "init failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            return 1;
        }
        if (!find_sym(&img, "req") || !find_sym(&img, "pick") || !find_sym(&img, "hop") ||
            !find_sym(&img, "stack_top") || !find_sym(&img, "stubs") || !find_sym(&img, "tag")) {
            fprintf(stderr, "init missing symbols\n");
            return 1;
        }
        if (!img.sec[CHRISO_SEC_DATA] || img.sec_size[CHRISO_SEC_DATA] < sizeof(magic) ||
            memcmp(img.sec[CHRISO_SEC_DATA], magic, sizeof(magic)) != 0) {
            fprintf(stderr, "designated initializer data mismatch\n");
            return 1;
        }
        if (!strstr(kcc_last_asm(), "cmp rax, 1") || !strstr(kcc_last_asm(), "jmp done")) {
            fprintf(stderr, "switch/goto asm:\n%s\n", kcc_last_asm());
            return 1;
        }
    }
    {
        static const char src[] =
            "uint64_t read_crs(void) {\n"
            "    uint64_t v;\n"
            "    __asm__ volatile (\"mov %%cr3, %0\" : \"=r\"(v));\n"
            "    __asm__ volatile (\"mov %%cr2, %0\" : \"=r\"(v));\n"
            "    __asm__ volatile (\"mov %%rsp, %0\" : \"=r\"(v));\n"
            "    return v;\n"
            "}\n"
            "void write_cr(uint64_t v, uint64_t page) {\n"
            "    __asm__ volatile (\"mov %0, %%cr3\" : : \"r\"(v) : \"memory\");\n"
            "    __asm__ volatile (\"invlpg (%0)\" : : \"r\"(page) : \"memory\");\n"
            "}\n"
            "struct IdtPointer { uint16_t limit; uint64_t base; };\n"
            "struct IdtPointer idt_pointer;\n"
            "void load_idt(void) {\n"
            "    __asm__ volatile (\"lidt %0\" : : \"m\"(idt_pointer) : \"memory\");\n"
            "}\n"
            "int cas32(uint32_t *p, uint32_t e, uint32_t d) {\n"
            "    return __sync_bool_compare_and_swap(p, e, d);\n"
            "}\n"
            "uint32_t add32(uint32_t *p, uint32_t d) {\n"
            "    return (uint32_t)__sync_fetch_and_add(p, d);\n"
            "}\n"
            "void rel32(uint32_t *p) { __sync_lock_release(p); }\n"
            "uint64_t retaddr(void) { return (uint64_t)__builtin_return_address(0); }\n";
        static const uint8_t cr3[] = {0x0f, 0x20, 0xd8};
        static const uint8_t cr2[] = {0x0f, 0x20, 0xd0};
        static const uint8_t wr[] = {0x0f, 0x22, 0xd8};
        static const uint8_t inv[] = {0x0f, 0x01, 0x38};
        static const uint8_t lid[] = {0x0f, 0x01, 0x18};
        static const uint8_t cas[] = {0xf0, 0x0f, 0xb1, 0x11};
        static const uint8_t xadd[] = {0xf0, 0x0f, 0xc1, 0x01};
        static const uint8_t sete[] = {0x0f, 0x94, 0xc0};
        static const uint8_t zx[] = {0x0f, 0xb6, 0xc0};
        static const uint8_t ra[] = {0x48, 0x8b, 0x45, 0x08};
        static const uint8_t rel[] = {0x89, 0x01};
        if (kcc_compile_named("priv.c", src, &img) != 0) {
            diag = kcc_last_error();
            fprintf(stderr, "priv failed: %s:%d:%d: %s\n", diag->file, diag->line,
                    diag->column, diag->message);
            return 1;
        }
        if (!text_has(&img, cr3, 3) || !text_has(&img, cr2, 3) || !text_has(&img, wr, 3) ||
            !text_has(&img, inv, 3) || !text_has(&img, lid, 3) || !text_has(&img, cas, 4) ||
            !text_has(&img, xadd, 4) || !text_has(&img, sete, 3) || !text_has(&img, zx, 3) ||
            !text_has(&img, ra, 4) || !text_has(&img, rel, 2)) {
            fprintf(stderr, "privileged or atomic bytes missing\n");
            return 1;
        }
    }
    if (compile_fails("err.c", "#error stop\n", "#error") != 0 ||
        compile_fails("break.c", "void x(void) { break; }\n", "break outside loop") != 0 ||
        compile_fails("assert0.c", "_Static_assert(0, \"no\");\n", "static assert failed") != 0 ||
        compile_fails("assertn.c", "void x(int n) { _Static_assert(n, \"n\"); }\n",
                      "constant expression expected") != 0 ||
        compile_fails("iretq.c",
                      "void x(void) { __asm__ volatile (\"iretq\"); }\n",
                      "asm template is outside this subset") != 0 ||
        compile_fails("syncn.c", "int x(int *p) { return __sync_fetch_and_sub(p, 1); }\n",
                      "builtin is outside this subset") != 0 ||
        compile_fails("retn.c",
                      "uint64_t x(void) { return (uint64_t)__builtin_return_address(1); }\n",
                      "builtin is outside this subset") != 0 ||
        compile_fails("flt.c",
                      "float add(float a, float b) { return a + b; }\n",
                      "float is outside this subset") != 0) {
        return 1;
    }
    puts("test_kcc: ok");
    return 0;
}
