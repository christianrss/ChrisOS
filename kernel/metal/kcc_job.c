#include "kcc_job.h"
#include "job.h"
#include "serial.h"
#include "kcc.h"
#include "chriso.h"
#include "fs.h"
#include "heap.h"

#define KCC_JOB_MAX 65536u

static char g_kcc_path[512];

static void kcc_worker(void *arg, uint32_t cpu_index) {
    const char *path = (const char *)arg;
    char *src_buf;
    uint8_t *out_buf;
    ChrisoImage img;
    char out_path[520];
    int n;
    int wn;
    int i;

    serial_puts("kcc job path=");
    serial_puts(path ? path : "(null)");
    serial_puts(" cpu=");
    serial_write_u64(cpu_index);
    serial_puts("\n");
    if (!path) {
        return;
    }
    src_buf = (char *)kmalloc(KCC_JOB_MAX);
    out_buf = (uint8_t *)kmalloc(KCC_JOB_MAX);
    if (!src_buf || !out_buf) {
        serial_puts("kcc job nomem\n");
        if (src_buf) {
            kfree(src_buf);
        }
        if (out_buf) {
            kfree(out_buf);
        }
        return;
    }
    n = fs_read(path, src_buf, KCC_JOB_MAX - 1);
    if (n < 0) {
        serial_puts("kcc job read fail\n");
        goto done;
    }
    src_buf[n] = 0;
    if (kcc_compile_source(src_buf, &img) != 0) {
        serial_puts("kcc job compile fail\n");
        goto done;
    }
    wn = chriso_write(&img, out_buf, KCC_JOB_MAX);
    if (wn < 0) {
        serial_puts("kcc job chriso fail\n");
        goto done;
    }
    i = 0;
    while (path[i] && i < 500) {
        out_path[i] = path[i];
        i++;
    }
    if (i + 7 < 519) {
        out_path[i++] = '.';
        out_path[i++] = 'C';
        out_path[i++] = 'H';
        out_path[i++] = 'R';
        out_path[i++] = 'I';
        out_path[i++] = 'S';
        out_path[i++] = 'O';
        out_path[i] = 0;
        if (fs_write(out_path, out_buf, wn) == wn) {
            serial_puts("kcc job ok\n");
        }
    }

done:
    kfree(src_buf);
    kfree(out_buf);
}

int kcc_job_submit_path(const char *path) {
    int i = 0;
    if (!path) {
        return -1;
    }
    while (path[i] && i < 511) {
        g_kcc_path[i] = path[i];
        i++;
    }
    g_kcc_path[i] = 0;
    return job_submit(kcc_worker, g_kcc_path);
}
