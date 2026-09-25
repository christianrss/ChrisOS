#ifndef CHRIS_LANG_PIPELINE_H
#define CHRIS_LANG_PIPELINE_H
#include <stdint.h>
#include "clvm/clvm_vm.h"
#include "editor.h"
#include "chrisc/chrisc.h"

#define LANG_VM_SLOTS 16
#define LANG_VM_BUDGET 64000u
#define LANG_VM_BUDGET_UI 4000000u
#define LANG_VM_BUDGET_GAME 20000000u
#define LANG_SPLASH_SLOT 0

void lang_init(ClvmSysFn sys, void *sys_user);
void lang_force_interp(void);
void lang_make_cc(void);
int lang_save(Editor *editor);
int lang_compile(Editor *editor);
int lang_run(Editor *editor, const char *clv_name);
int lang_run_path(const char *clv_name);
int lang_run_path_replace(const char *clv_name);
int lang_run_path_arg(const char *clv_name, const char *arg);
void lang_set_app_arg(const char *arg);
int lang_copy_app_arg(char *out, int cap);
int lang_compile_path(const char *src_path);
int lang_run_jit(Editor *editor, const char *clv_name);
int lang_compile_run(Editor *editor);
int lang_compile_run_jit(Editor *editor);
void lang_tick(uint32_t now);
int lang_active_count(void);
int lang_kill(int slot);
int lang_slot_used(int slot);
const char *lang_slot_name(int slot);
uint32_t lang_slot_caps(int slot);

#define CAP_PCI 1u
#define CAP_PORT_IO 2u
#define CAP_MMIO 4u
#define CAP_DMA 8u
#define CAP_IRQ 16u
#define CAP_DISK_ADMIN 32u
#define CAP_DRIVER                                                         \
    (CAP_PCI | CAP_PORT_IO | CAP_MMIO | CAP_DMA | CAP_IRQ | CAP_DISK_ADMIN)
uint32_t *lang_slot_pixels(int slot);
void lang_slot_publish(int slot);
int lang_slot_w(int slot);
int lang_slot_h(int slot);
int lang_slot_fullscreen(int slot);
int lang_slot_task(int slot);
void lang_bind_task(int slot, int task_id);
int lang_find_slot_by_task(int task_id);
int lang_find_slot_by_gfx(const void *gfx_ctx);
void lang_slot_push_key(int slot, int key);
void lang_slot_push_text(int slot, int ch);
int lang_slot_take_key(int slot);
int lang_slot_take_text(int slot);
void lang_slot_request_close(int slot);
int lang_compile_file(const char *src_path, const char *clv_path);
int lang_compile_many(const char **paths, int npaths);
int lang_compile_list(const char *lst_path);
const char *lang_last_clv(void);
const char *lang_last_error(void);
int lang_splash_start(const char *clv_path);
void lang_splash_frame(uint32_t now);
void lang_splash_stop(void);
void lang_debug_enable(int on);
void lang_debug_step(void);
void lang_debug_step_over(void);
void lang_debug_step_out(void);
void lang_debug_continue(void);
int lang_debug_paused(void);
uint32_t lang_debug_pc(void);
int64_t lang_debug_stack(int i);
int32_t lang_debug_mem(uint32_t addr);
int lang_bp_add(uint32_t pc);
int lang_bp_toggle_line(int line);
uint16_t lang_debug_line(void);
void lang_write_map(const char *clv_path, const ChrisResult *r);
int lang_hot_reload(const char *clv_name);
int lang_checkpoint_save(int slot, const uint8_t *bytes, int n, uint32_t off);
uint32_t lang_debug_call(int depth);
const char *lang_debug_fn(uint32_t pc);
void lang_debug_set_watch(uint32_t addr);
uint32_t lang_debug_watch(void);
int lang_debug_sys(int index, int *id);
int lang_debug_fault(uint64_t *cr2, int *pid, uint64_t *rip);

#endif
