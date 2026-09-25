#ifndef CHRIS_ARCH_H
#define CHRIS_ARCH_H

#include <stdint.h>

/* Shared architectural state. ChrisCPU executes it. ChrisHV will import and
 * export the same struct to a VMCS or VMCB. Do not fork a second model. */

#define CHRIS_GPR_RAX 0
#define CHRIS_GPR_RCX 1
#define CHRIS_GPR_RDX 2
#define CHRIS_GPR_RBX 3
#define CHRIS_GPR_RSP 4
#define CHRIS_GPR_RBP 5
#define CHRIS_GPR_RSI 6
#define CHRIS_GPR_RDI 7

#define CHRIS_CR0_PE (1ull << 0)
#define CHRIS_CR0_NE (1ull << 5)
#define CHRIS_CR0_WP (1ull << 16)
#define CHRIS_CR0_PG (1ull << 31)

#define CHRIS_CR4_PAE (1ull << 5)

#define CHRIS_EFER 0xC0000080u
#define CHRIS_EFER_SCE (1ull << 0)
#define CHRIS_EFER_LME (1ull << 8)
#define CHRIS_EFER_LMA (1ull << 10)
#define CHRIS_EFER_NXE (1ull << 11)

#define CHRIS_MSR_STAR 0xC0000081u
#define CHRIS_MSR_LSTAR 0xC0000082u
#define CHRIS_MSR_CSTAR 0xC0000083u
#define CHRIS_MSR_FMASK 0xC0000084u
#define CHRIS_MSR_FS_BASE 0xC0000100u
#define CHRIS_MSR_GS_BASE 0xC0000101u
#define CHRIS_MSR_KERNEL_GS 0xC0000102u
#define CHRIS_MSR_APIC_BASE 0x0000001Bu

#define CHRIS_EX_DE 0
#define CHRIS_EX_DB 1
#define CHRIS_EX_BP 3
#define CHRIS_EX_UD 6
#define CHRIS_EX_DF 8
#define CHRIS_EX_TS 10
#define CHRIS_EX_NP 11
#define CHRIS_EX_SS 12
#define CHRIS_EX_GP 13
#define CHRIS_EX_PF 14

#define CHRIS_BOOT_PROTOCOL 1

enum {
    CHRIS_EXIT_NONE = 0,
    CHRIS_EXIT_HLT,
    CHRIS_EXIT_SHUTDOWN,
    CHRIS_EXIT_EXCEPTION,
    CHRIS_EXIT_TRIPLE,
    CHRIS_EXIT_BREAK,
    CHRIS_EXIT_STEP_LIMIT,
    CHRIS_EXIT_UNMAPPED
};

enum {
    CHRIS_OP_NONE = 0,
    CHRIS_OP_ALU,
    CHRIS_OP_MOV,
    CHRIS_OP_MOVZX,
    CHRIS_OP_MOVSX,
    CHRIS_OP_LEA,
    CHRIS_OP_XCHG,
    CHRIS_OP_PUSH,
    CHRIS_OP_POP,
    CHRIS_OP_PUSHF,
    CHRIS_OP_POPF,
    CHRIS_OP_JMP,
    CHRIS_OP_JCC,
    CHRIS_OP_CALL,
    CHRIS_OP_RET,
    CHRIS_OP_SHIFT,
    CHRIS_OP_UNARY,
    CHRIS_OP_MULDIV,
    CHRIS_OP_IN,
    CHRIS_OP_OUT,
    CHRIS_OP_INT,
    CHRIS_OP_IRETQ,
    CHRIS_OP_HLT,
    CHRIS_OP_NOP,
    CHRIS_OP_FLAG,
    CHRIS_OP_LEAVE,
    CHRIS_OP_CPUID,
    CHRIS_OP_RDMSR,
    CHRIS_OP_WRMSR,
    CHRIS_OP_MOVCR,
    CHRIS_OP_DESC,
    CHRIS_OP_SETCC,
    CHRIS_OP_CMOV,
    CHRIS_OP_UD,
    CHRIS_OP_UNIMPL
};

enum {
    CHRIS_ALU_ADD = 0,
    CHRIS_ALU_OR,
    CHRIS_ALU_ADC,
    CHRIS_ALU_SBB,
    CHRIS_ALU_AND,
    CHRIS_ALU_SUB,
    CHRIS_ALU_XOR,
    CHRIS_ALU_CMP,
    CHRIS_ALU_TEST
};

enum {
    CHRIS_FORM_RM_REG = 0,
    CHRIS_FORM_REG_RM,
    CHRIS_FORM_RM_IMM,
    CHRIS_FORM_ACC_IMM
};

enum {
    CHRIS_SH_ROL = 0,
    CHRIS_SH_ROR,
    CHRIS_SH_SHL,
    CHRIS_SH_SHR,
    CHRIS_SH_SAR
};

enum {
    CHRIS_UN_NOT = 0,
    CHRIS_UN_NEG,
    CHRIS_UN_INC,
    CHRIS_UN_DEC
};

enum {
    CHRIS_MD_MUL = 0,
    CHRIS_MD_IMUL,
    CHRIS_MD_DIV,
    CHRIS_MD_IDIV,
    CHRIS_MD_IMUL2
};

enum {
    CHRIS_FLAG_CLC = 0,
    CHRIS_FLAG_STC,
    CHRIS_FLAG_CLD,
    CHRIS_FLAG_STD,
    CHRIS_FLAG_CLI,
    CHRIS_FLAG_STI
};

enum {
    CHRIS_DESC_SGDT = 0,
    CHRIS_DESC_SIDT,
    CHRIS_DESC_LGDT,
    CHRIS_DESC_LIDT
};

typedef struct ChrisSeg {
    uint16_t sel;
    uint64_t base;
    uint32_t limit;
    uint16_t attr;
} ChrisSeg;

typedef struct ChrisDtr {
    uint64_t base;
    uint16_t limit;
} ChrisDtr;

typedef struct ChrisArchitectureState {
    union {
        uint64_t gpr[16];
        struct {
            uint64_t rax;
            uint64_t rcx;
            uint64_t rdx;
            uint64_t rbx;
            uint64_t rsp;
            uint64_t rbp;
            uint64_t rsi;
            uint64_t rdi;
            uint64_t r8;
            uint64_t r9;
            uint64_t r10;
            uint64_t r11;
            uint64_t r12;
            uint64_t r13;
            uint64_t r14;
            uint64_t r15;
        };
    };
    uint64_t rip;
    uint64_t rflags;
    uint64_t cr0;
    uint64_t cr2;
    uint64_t cr3;
    uint64_t cr4;
    uint64_t cr8;
    ChrisSeg cs, ds, es, fs, gs, ss;
    ChrisSeg tr, ldtr;
    ChrisDtr gdtr, idtr;
    uint64_t efer;
    uint64_t star, lstar, cstar, fmask;
    uint64_t fs_base, gs_base, kernel_gs_base;
    uint64_t apic_base;
    uint64_t tsc;
    uint8_t xmm[16][16];
    int cpl;
} ChrisArchitectureState;

typedef struct ChrisInsn {
    int op;
    int len;
    int os;
    int asz;
    int rex;
    int rex_w, rex_r, rex_x, rex_b;
    int mod, reg, rm, rm_field, digit;
    int has_modrm;
    int has_sib;
    int scale, index, index_field, base, base_field;
    int no_base;
    int no_index;
    int rip_rel;
    int64_t disp;
    int has_disp;
    uint64_t imm;
    int imm_bytes;
    int cc;
    int alu;
    int form;
    int src_os;
    int lock;
    int shift_kind;
    int shift_imm;
    int shift_cl;
    int unary;
    int muldiv;
    int flag_op;
    int vector;
    int cr_to_reg;
    int desc_op;
    int acc_imm;
    int reg_only_push;
    int imm_src;
} ChrisInsn;

struct ChrisMachine;
struct ChrisCpu;

typedef struct ChrisCpuBackend {
    const char *name;
    int (*init)(struct ChrisMachine *machine);
    int (*create_cpu)(struct ChrisMachine *machine, unsigned cpu_id);
    int (*reset)(struct ChrisCpu *cpu);
    int (*run)(struct ChrisCpu *cpu, uint64_t max_steps);
    void (*inject_irq)(struct ChrisCpu *cpu, uint8_t vector);
    void (*get_state)(const struct ChrisCpu *cpu, ChrisArchitectureState *out);
    void (*set_state)(struct ChrisCpu *cpu, const ChrisArchitectureState *in);
    void (*invalidate_tlb)(struct ChrisCpu *cpu);
    void (*shutdown)(struct ChrisCpu *cpu);
} ChrisCpuBackend;

const char *chris_op_name(int op);
int chris_decode(const uint8_t *bytes, int avail, ChrisInsn *out);
void chris_format_insn(const ChrisInsn *in, char *dst, int cap);

void chris_arch_reset(ChrisArchitectureState *st);
int chris_cc_true(int cc, uint64_t rflags);
void chris_cpuid(uint32_t leaf, uint32_t sub, uint32_t *eax, uint32_t *ebx, uint32_t *ecx,
                 uint32_t *edx);

#endif
