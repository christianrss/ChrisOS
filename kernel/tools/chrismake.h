#ifndef CHRIS_CHRISMAKE_H
#define CHRIS_CHRISMAKE_H

#include <stdint.h>

#define CHRISMAKE_MSG 160

typedef int (*ChrisMakeRecipeFn)(void *user, const char *recipe, char *err,
                                 int err_cap);
typedef int (*ChrisMakeStampFn)(void *user, const char *path, uint64_t *mtime);

int chrismake_run(const char *text, const char *target, ChrisMakeRecipeFn run,
                  void *user, char *err, int err_cap);
int chrismake_run_stamped(const char *text, const char *target,
                          ChrisMakeRecipeFn run, ChrisMakeStampFn stamp,
                          void *user, char *err, int err_cap);
int chrismake_first_recipe(const char *text, const char *target, char *out,
                           int cap);

#endif
