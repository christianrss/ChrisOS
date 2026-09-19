#ifndef CHRIS_LANG_PIPELINE_H
#define CHRIS_LANG_PIPELINE_H
#include <stdint.h>
#include "clvm/clvm_vm.h"
#include "editor.h"

#define LANG_VM_SLOTS 8
#define LANG_VM_BUDGET 2000u

void lang_init(ClvmSysFn sys, void *sys_user);
int lang_save(Editor *editor);
int lang_compile(Editor *editor);
int lang_run(Editor *editor, const char *clv_name);
int lang_run_jit(Editor *editor, const char *clv_name);
int lang_compile_run(Editor *editor);
int lang_compile_run_jit(Editor *editor);
void lang_tick(uint32_t now);
int lang_active_count(void);
int lang_kill(int slot);
int lang_slot_used(int slot);
const char *lang_slot_name(int slot);
uint32_t *lang_slot_pixels(int slot);
int lang_slot_task(int slot);
void lang_bind_task(int slot, int task_id);
int lang_find_slot_by_task(int task_id);

#endif
