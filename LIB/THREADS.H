#ifndef THREADS_H
#define THREADS_H
int thrd_create(int (*fn)(void *), void *arg);
int thrd_join(int id);
int mtx_lock(int *m);
int mtx_unlock(int *m);
int cnd_wait(int *c, int *m);
int cnd_signal(int *c);
#endif
