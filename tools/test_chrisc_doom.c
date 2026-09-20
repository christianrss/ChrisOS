#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "chrisc.h"
#include "clvm.h"
#include "clvm_vm.h"

static int sys_ok(ClvmVm *vm, int32_t id, void *user) {
    (void)vm;
    (void)id;
    (void)user;
    return -1;
}

static int read_file(void *user, const char *path, char *out, int cap) {
    FILE *f;
    size_t n;
    (void)user;
    f = fopen(path, "rb");
    if (!f)
        return -1;
    n = fread(out, 1, (size_t)cap - 1u, f);
    fclose(f);
    out[n] = 0;
    return (int)n;
}

static int32_t rd32(const uint8_t *m, uint32_t off) {
    return (int32_t)((uint32_t)m[off] | ((uint32_t)m[off + 1] << 8) |
                     ((uint32_t)m[off + 2] << 16) | ((uint32_t)m[off + 3] << 24));
}

static int run_first_int(const char *src, int32_t expect, const char *tag) {
    uint8_t code[65536];
    uint8_t file[70000];
    ChrisResult r;
    ClvmImage img;
    ClvmVm vm;
    size_t n;
    ClvmStepResult st;

    memset(&vm, 0, sizeof(vm));
    memset(&r, 0, sizeof(r));
    if (!chrisc_compile(src, strlen(src), code, sizeof(code), &r)) {
        fprintf(stderr, "%s compile fail %s:%d %s\n", tag, r.diag.file, r.diag.line,
                r.diag.message);
        return 1;
    }
    n = clvm_write_image_v2(file, sizeof(file), 0, r.entry, 0, code, r.code_size);
    if (!n || clvm_parse(file, n, &img) != CL_LOAD_OK) {
        fprintf(stderr, "%s image fail\n", tag);
        return 1;
    }
    clvm_vm_init(&vm, &img, sys_ok, 0);
    st = clvm_step(&vm, 2000000);
    if (st != CLVM_STEP_HALT) {
        fprintf(stderr, "%s run %d fault %d pc %u\n", tag, (int)st, (int)vm.fault,
                vm.pc);
        return 1;
    }
    if (rd32(vm.memory, 0) != expect) {
        fprintf(stderr, "%s got %d want %d\n", tag, rd32(vm.memory, 0), expect);
        return 1;
    }
    return 0;
}

int main(void) {
    if (run_first_int(
            "#define FixedMul(a,b) ((a)*(b))\n"
            "void main(){ int r; r = FixedMul(3, 7); }\n",
            21, "macro-args"))
        return 1;

    if (run_first_int(
            "void main(){ int n; int tab[4]; n = sizeof(tab); }\n",
            16, "sizeof-array"))
        return 1;

    if (run_first_int(
            "void main(){\n"
            " int a; int tab[4]; int *p;\n"
            " tab[0]=1; tab[1]=7; tab[2]=3; tab[3]=4;\n"
            " p = &tab; p = p + 1; a = *p;\n"
            "}\n",
            7, "ptr-add"))
        return 1;

    if (run_first_int(
            "int z;\n"
            "void bump(int *p){ *p = *p + 1; }\n"
            "void main(){ z = 40; bump(&z); }\n",
            41, "deref-assign"))
        return 1;

    if (run_first_int(
            "int z;\n"
            "struct Pt { int x; int y; };\n"
            "void setx(struct Pt *p, int v){ p->x = v; }\n"
            "void main(){ struct Pt o; setx(&o, 9); z = o.x; }\n",
            9, "struct-ptr-arg"))
        return 1;

    if (run_first_int(
            "void setup(void){ }\n"
            "void main(){ int z; setup(); z = 3; }\n",
            3, "void-args"))
        return 1;

    if (run_first_int(
            "int z;\n"
            "typedef struct Box { int w; int h; } Box;\n"
            "void grow(Box *b){ b->w = b->w + 1; }\n"
            "void main(){ Box o; o.w = 10; grow(&o); z = o.w; }\n",
            11, "typedef-struct"))
        return 1;

    {
        const char *src =
            "struct M {\n"
            " int a; int b; int c; int d; int e; int f; int g; int h;\n"
            " int i; int j; int k; int l; int m; int n; int o; int p;\n"
            " int q; int r; int s; int t; int u; int v; int w; int x;\n"
            " int y; int z; int a2; int b2; int c2; int d2; int e2; int f2;\n"
            " int g2; int h2; int i2; int j2; int k2; int l2; int m2; int n2;\n"
            "};\n"
            "void main(){ int z; struct M o; o.n2 = 41; z = o.n2; }\n";
        if (run_first_int(src, 41, "mobj-fields"))
            return 1;
    }

    {
        char src[4096];
        int i;
        int n = 0;
        n += sprintf(src + n, "void main(){ int x; int a; a = 70; switch(a){\n");
        for (i = 0; i < 80; i++)
            n += sprintf(src + n, " case %d: x = %d; break;\n", i, i + 100);
        n += sprintf(src + n, " default: x = 1;\n}}\n");
        if (run_first_int(src, 170, "switch-80"))
            return 1;
    }

    {
        const char *pa = "build/host/st_a.cc";
        const char *pb = "build/host/st_b.cc";
        const char *ps[2];
        FILE *f;
        uint8_t code[65536];
        uint8_t file[70000];
        ChrisResult r;
        ClvmImage img;
        ClvmVm vm;
        size_t n;
        f = fopen(pa, "wb");
        if (!f)
            return 1;
        fputs("int result; static int k; int getk(){ return k; } void setk(int v){ k = v; }\n",
              f);
        fclose(f);
        f = fopen(pb, "wb");
        if (!f)
            return 1;
        fputs("static int k; void main(){ setk(5); result = getk(); }\n",
              f);
        fclose(f);
        ps[0] = pa;
        ps[1] = pb;
        memset(&r, 0, sizeof(r));
        if (!chrisc_compile_files(ps, 2, read_file, 0, code, sizeof(code), &r)) {
            fprintf(stderr, "static-tu fail %s:%d %s\n", r.diag.file, r.diag.line,
                    r.diag.message);
            return 1;
        }
        n = clvm_write_image_v2(file, sizeof(file), 0, r.entry, 0, code, r.code_size);
        if (!n || clvm_parse(file, n, &img) != CL_LOAD_OK)
            return 1;
        memset(&vm, 0, sizeof(vm));
        clvm_vm_init(&vm, &img, sys_ok, 0);
        if (clvm_step(&vm, 2000000) != CLVM_STEP_HALT) {
            fprintf(stderr, "static-tu run fail\n");
            return 1;
        }
        if (rd32(vm.memory, 0) != 5) {
            fprintf(stderr, "static-tu got %d want 5\n", rd32(vm.memory, 0));
            return 1;
        }
    }

    if (run_first_int(
            "typedef enum { sk_noitems = -1, sk_baby = 0, sk_easy } skill_t;\n"
            "void main(){ int z; z = sk_noitems; }\n",
            -1, "enum-neg"))
        return 1;
    if (run_first_int(
            "typedef enum { BT_WEAPONMASK = (8+16+32) } buttoncode_t;\n"
            "void main(){ int z; z = BT_WEAPONMASK; }\n",
            56, "enum-paren"))
        return 1;
    if (run_first_int(
            "void main(){ int z; int tab[5*8/2]; z = sizeof(tab); }\n",
            80, "array-expr"))
        return 1;
    if (run_first_int(
            "typedef unsigned angle_t;\n"
            "void main(){ int z; angle_t a; a = 3; z = a; }\n",
            3, "typedef-unsigned"))
        return 1;
    if (run_first_int(
            "typedef struct { void (*init)(int *p); int x; } mode_t;\n"
            "void main(){ int z; mode_t m; m.x = 9; z = m.x; }\n",
            9, "fnptr-field"))
        return 1;
    if (run_first_int(
            "#define ORIG 4\n#define N (ORIG + 4)\n"
            "void main(){ int z; int tab[N]; z = sizeof(tab); }\n",
            32, "macro-nested"))
        return 1;
    if (run_first_int(
            "int gametic, ticdup;\n"
            "void main(){ int z; ticdup = 2; gametic = ticdup; z = gametic; }\n",
            2, "comma-global"))
        return 1;
    if (run_first_int(
            "int automapactive;\nint automapactive;\n"
            "void main(){ int z; automapactive = 4; z = automapactive; }\n",
            4, "redecl-global"))
        return 1;
    if (run_first_int(
            "void main(){ int z; int dx, dy; dx = 3; dy = 4; z = dx + dy; }\n",
            7, "comma-local"))
        return 1;
    if (run_first_int(
            "typedef struct { int x, y; } pt_t;\n"
            "typedef struct { pt_t a, b; } line_t;\n"
            "void main(){ int z; line_t ml; z = ml.a.y; }\n",
            0, "nested-field"))
        return 1;
    if (run_first_int(
            "int m_x;\n"
            "void main(){ int z; m_x = 10; m_x += 4; m_x -= 3; z = m_x; }\n",
            11, "compound-assign"))
        return 1;
    if (run_first_int(
            "void main(){ int z; int i; z = 0; for (i = 0; i < 3; i++) { z = z + 1; } }\n",
            3, "for-inc"))
        return 1;
    if (run_first_int(
            "void main(){ int z; static int lastlevel = -1, lastepisode = -1;\n"
            "lastlevel = 2; lastepisode = 3; z = lastlevel + lastepisode; }\n",
            5, "static-comma-init"))
        return 1;
    if (run_first_int(
            "#define PAN 4\n#define FTOM(x) ((x)<<16)\n"
            "void main(){ int z; z = FTOM(PAN); }\n",
            262144, "macro-fn-arg"))
        return 1;
    if (run_first_int(
            "void main(){ int z; int a[8]; int i; i = 0; a[0] = 3;\n"
            "z = a[i++]; }\n",
            3, "postinc-index"))
        return 1;
    if (run_first_int(
            "void main(){ int z; enum { LEFT = 1, RIGHT = 2 };\n"
            "register int oc; oc = 0; oc |= LEFT; z = oc; }\n",
            1, "local-enum-oreq"))
        return 1;
    if (run_first_int(
            "#define INNER(x) ((x)+2)\n#define OUTER(x) (INNER((x)+1))\n"
            "void main(){ int z; z = OUTER(3); }\n",
            6, "macro-fn-nested"))
        return 1;
    if (run_first_int(
            "typedef struct { int x; int y; } P;\n"
            "typedef struct { P a; P b; } L;\n"
            "void main(){ int z; L fl; fl.a.x = 7; z = fl.a.x; }\n",
            7, "nested-field-assign"))
        return 1;
    if (run_first_int(
            "#define DOOUT(oc, mx) (oc) = 0; if ((mx) < 0) (oc) |= 1;\n"
            "void main(){ int z; int oc; int mx; mx = -2; oc = 9;\n"
            "DOOUT(oc, mx); z = oc; }\n",
            1, "macro-multi-stmt"))
        return 1;
    if (run_first_int(
            "typedef struct { int powers[4]; } Pl;\n"
            "void main(){ int z; Pl p; Pl *pl; pl = &p; z = 3;\n"
            "if (pl->powers[0]) z = 3; }\n",
            3, "field-index"))
        return 1;
    if (run_first_int(
            "typedef struct { int x; int y; } V;\n"
            "typedef struct { V *v1; V *v2; } Line;\n"
            "void main(){ int z; V a; Line tab[2]; a.x = 11; tab[0].v1 = &a;\n"
            "z = tab[0].v1->x; }\n",
            11, "index-ptr-field"))
        return 1;
    if (run_first_int(
            "int f_x; int m_x; int scale_mtof;\n"
            "#define FixedMul(a,b) ((a)*(b))\n"
            "#define MTOF(x) (FixedMul((x),scale_mtof)>>16)\n"
            "#define CXMTOF(x) (f_x + MTOF((x)-m_x))\n"
            "typedef struct { int x; int y; } P;\n"
            "typedef struct { P a; P b; } L;\n"
            "void tick(L *fl, L *ml){ fl->a.x = CXMTOF(ml->a.x); }\n"
            "void main(){ int z; L a; L b; f_x = 0; m_x = 0; scale_mtof = 1;\n"
            "tick(&a, &b); f_x = 1; }\n",
            1, "cxmtof-assign"))
        return 1;
    if (run_first_int(
            "void main(){ int z; static const struct { char *name; int n; } packs[2];\n"
            "z = 1; }\n",
            1, "anon-struct-array"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "#define PACKAGE_STRING \"Doom Generic 0.1\"\n"
            "void banner(char *t){ t = t; }\n"
            "void main(){ z = 1; banner(PACKAGE_STRING); }\n",
            1, "package-string"))
        return 1;
    if (run_first_int(
            "void main(){ int z; z = 0; { z = 1; } }\n",
            1, "nested-block"))
        return 1;
    if (run_first_int(
            "void main(){ int z; z = +3; }\n",
            3, "unary-plus"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "#define DEH_String(x) (x)\n"
            "void banner(char *t){ t = t; }\n"
            "void main(){ z = 1; banner(DEH_String(\"hello \"\n"
            "    \"world\")); }\n",
            1, "macro-span-strings"))
        return 1;
    if (run_first_int(
            "#if 1\nint z;\n#elif 1\n#error no\n#else\n#error no\n#endif\n"
            "void main(){ z = 1; }\n",
            1, "if-elif-else"))
        return 1;
    if (run_first_int(
            "#define BIG \\\n"
            "\"abcdefghij\" \\\n"
            "\"klmnopqrst\"\n"
            "int z;\n"
            "void eat(char *s){ s = s; }\n"
            "void main(){ z = 1; eat(BIG); }\n",
            1, "macro-line-cont"))
        return 1;
    if (run_first_int(
            "#define START '!' // first\n"
            "#define END '_' // last\n"
            "#define SIZE (END - START + 1)\n"
            "int z;\n"
            "void main(){ z = SIZE; }\n",
            63, "char-const-macro"))
        return 1;
    if (run_first_int(
            "int z; int cell; int *s;\n"
            "void main(){ cell = 4; s = &((int *)&cell)[0]; z = *s; }\n",
            4, "cast-index-addr"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "int add1(int a, int b, int c){ return a + b + c; }\n"
            "void main(){\n"
            "  int (*wipes[4])(int, int, int);\n"
            "  wipes[0] = add1;\n"
            "  z = 1;\n"
            "}\n",
            1, "fnptr-array"))
        return 1;
    if (run_first_int(
            "typedef struct { int inn; int skills; } Pl;\n"
            "typedef struct { Pl plyr[4]; } Wb;\n"
            "int z;\n"
            "void main(){ Wb w; int i; i = 0; w.plyr[i].inn = 7; z = w.plyr[i].inn; }\n",
            7, "struct-array-field"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "void main(){ int y; int off; int i; y = 0; off = 0;\n"
            "for (y=1, off=2; y<3; y++, off+=10) {}\n"
            "z = y + off; }\n",
            25, "for-comma"))
        return 1;
    if (run_first_int(
            "enum { newgame = 0, options, main_end } main_e;\n"
            "int z;\n"
            "void main(){ z = options; }\n",
            1, "enum-trailing-name"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "void (*messageRoutine)(int response);\n"
            "void main(){ messageRoutine = 0; z = 1; }\n",
            1, "global-fnptr"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "struct M { int x; int y; struct M *target; };\n"
            "void main(){ struct M a; struct M b; a.x = 7; b.target = &a; z = b.target->x; }\n",
            7, "ptr-field-chain"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "void main(){ int d; d = 3; if ((unsigned)d >= 8) z = 0; else z = 1; }\n",
            1, "cast-unsigned"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "void main(){ unsigned an; an = 4; z = an; }\n",
            4, "unsigned-bare"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "struct M { int type; int x; };\n"
            "void main(){ struct M o; struct M *p; o.type = 9; p = &o; z = ((struct M *)p)->type; }\n",
            9, "cast-arrow"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "struct Side { int sector; };\n"
            "struct Side g;\n"
            "struct Side *gs(int n){ n = n; return &g; }\n"
            "void main(){ g.sector = 11; z = gs(0)->sector; }\n",
            11, "call-arrow"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "struct L { int flags; };\n"
            "struct L g;\n"
            "struct L *arr[2];\n"
            "struct S { struct L **lines; };\n"
            "void main(){ struct S s; arr[0] = &g; g.flags = 13; s.lines = arr; (s.lines[0])->flags; z = 1; }\n",
            1, "index-arrow"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "typedef struct { int flags; } line_t;\n"
            "typedef struct {\n"
            "  int isaline;\n"
            "  union { int *thing; line_t *line; } d;\n"
            "} intercept_t;\n"
            "void main(){ intercept_t in; line_t L; L.flags = 5; in.d.line = &L; z = in.d.line->flags; }\n",
            5, "named-union"))
        return 1;
    if (run_first_int(
            "#define F1(x,y,z) (z ^ (x & (y ^ z)))\n"
            "#define R(a,f) (a + f(1,2,3))\n"
            "int z;\n"
            "void main(){ z = R(4, F1); }\n",
            6, "macro-as-arg"))
        return 1;
    if (run_first_int(
            "#define M(i) (tm = x[0] ^ x[1], (x[0] = tm))\n"
            "int z;\n"
            "void main(){ int x[2]; int tm; int e; x[0] = 5; x[1] = 7; e = 1; e = e + M(0); z = e; }\n",
            3, "assign-comma-value"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "void main(){ int v; int *p; v = 4; p = &v; z = (*p = 9); }\n",
            9, "deref-assign-value"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "struct M { int x; int y; };\n"
            "void main(){ struct M a; int *p; a.x = 0; a.y = 0; p = &a.x; *p = 7; z = a.x; }\n",
            7, "addr-field"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "struct M { int x; int y; };\n"
            "void main(){ struct M a; struct M *q; int *p; a.x = 0; a.y = 0; q = &a; p = &q->x; *p = 8; z = a.x; }\n",
            8, "addr-arrow"))
        return 1;
    if (run_first_int(
            "int z;\n"
            "int arr[4];\n"
            "void main(){ int *p; arr[0] = 0; arr[1] = 0; arr[2] = 0; arr[3] = 0; p = &arr[2]; *p = 11; z = arr[2]; }\n",
            11, "addr-index"))
        return 1;

    printf("test_chrisc_doom: ok\n");
    return 0;
}
