#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "cla/cla.h"
#include "gc/gc.h"
#include "il/il.h"
#include "clvm/clvm.h"

int main(void) {
    ClaImage img;
    uint8_t buf[256];
    uint8_t il[8];
    IlSig sig;
    void *p;
    int n;

    memset(&img, 0, sizeof(img));
    memcpy(img.name, "LIB", 4);
    img.version = 1;
    img.nmethods = 1;
    memcpy(img.methods[0].name, "main", 5);
    img.methods[0].rva = 0;
    img.methods[0].size = 1;
    img.methods[0].sig.ret = IL_I4;
    il[0] = CL_OP_RET;
    img.il = il;
    img.il_size = 1;
    n = cla_write(buf, sizeof(buf), &img);
    assert(n > 0);
    cla_reset();
    assert(cla_load_bytes(buf, (size_t)n));
    {
        ClaImage lib;
        uint8_t buf2[256];
        int n2;
        memset(&lib, 0, sizeof(lib));
        memcpy(lib.name, "APP", 4);
        lib.version = 1;
        lib.nmethods = 1;
        lib.nrefs = 1;
        memcpy(lib.refs[0], "LIB", 4);
        memcpy(lib.methods[0].name, "go", 3);
        lib.methods[0].size = 1;
        lib.il = il;
        lib.il_size = 1;
        n2 = cla_write(buf2, sizeof(buf2), &lib);
        assert(n2 > 0);
        assert(cla_load_bytes(buf2, (size_t)n2));
        assert(cla_count() == 2);
    }
    sig.ret = IL_I4;
    sig.argc = 0;
    assert(il_verify(il, 1, &sig));
    gc_init();
    p = gc_alloc(1, 16);
    assert(p);
    gc_root_reg(&p);
    gc_collect();
    assert(gc_live() == 1);
    p = 0;
    gc_collect();
    assert(gc_live() == 0);
    printf("test_cla_gc ok\n");
    return 0;
}
