#ifndef CHRIS_CHRISMAKE_H
#define CHRIS_CHRISMAKE_H

#define CHRISMAKE_MSG 160

typedef int (*ChrisMakeRecipeFn)(void *user, const char *recipe, char *err,
                                 int err_cap);

int chrismake_run(const char *text, const char *target, ChrisMakeRecipeFn run,
                  void *user, char *err, int err_cap);
int chrismake_first_recipe(const char *text, const char *target, char *out,
                           int cap);

#endif
