#ifndef SETJMP_H
#define SETJMP_H
typedef long jmp_buf[64];
int setjmp(jmp_buf env);
void longjmp(jmp_buf env, int val);
#endif
