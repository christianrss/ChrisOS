/* LEARN:STOR64-S07 */
#include <stdint.h>
#include "bootinfo.h"
#include "desktop.h"
#include "fs.h"
#include "gdt.h"
#include "graphics.h"
#include "heap.h"
#include "idt.h"
#include "irq.h"
#include "mm.h"
#include "panic.h"
#include "pit.h"
#include "pmm.h"
#include "ps2.h"
#include "lang_pipeline.h"
#include "serial.h"
#include "storage.h"
#include "syscall.h"
#include "clvm_sys.h"
#include "speaker.h"
#include "net.h"
#include "apic.h"
#include "ioapic.h"
#include "job.h"
#include "smp.h"
#include "sse_init.h"
#include "boot_splash.h"
#include "proc.h"
#include "ac97.h"
#include "hwgate.h"
#include "acpi.h"
#include "install.h"

void kstart(void) {
    const struct bootinfo *boot;

    if (!serial_init()) {
        __asm__ volatile ("cli");
        for (;;) {
            __asm__ volatile ("hlt");
        }
    }
    serial_puts("ChrisOS selfhost=1\n");

    bootinfo_init();
    gdt_init();
    idt_init();
    syscall_init();
    pic_init();
    if (!pit_init(60)) {
        panic("frequencia PIT invalida");
    }
    if (!ps2_init()) {
        serial_puts("ChrisOS: PS/2 unavailable (keyboard/mouse disabled)\n");
    }

    pmm_init();
    pmm_selftest();
    mm_init();
    mm_selftest();
    heap_init();
    sse_bsp_init();
    heap_selftest();
    proc_init();

    boot = bootinfo_get();
    if (boot->fb_bpp != 32 ||
        !gfx_init((uint32_t *)(uintptr_t)boot->fb_addr,
                  (int)boot->fb_width,
                  (int)boot->fb_height,
                  (int)boot->fb_pitch)) {
        panic("gfx_init recusou o framebuffer");
    }
    (void)virtio_gpu_boot();
    gfx_clear(0x00101828u);
    gfx_present();

    apic_init();
    ioapic_init();
    job_init();
    smp_init();
    smp_job_selftest();

    __asm__ volatile ("cli");
    acpi_probe();
    storage_init();
    fs_init();
    (void)install_selftest();
    (void)install_auto();
    lang_init(clvm_sys_dispatch, 0);
    lang_make_cc();
    speaker_off();
    (void)ac97_init();

    __asm__ volatile ("sti");
    desktop_init();
    desktop_boot_apps();
    gfx_present();
    if (!net_init()) {
        serial_puts("ChrisOS: net unavailable\n");
    }
    serial_puts("ChrisOS: desktop 60Hz\n");
    desktop_run();
}

