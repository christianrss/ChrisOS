int g_n;
int g_lock;

void worker(int arg) {
  int i;
  i = 0;
  while (i < 1000) {
    mtx_lock(g_lock);
    g_n = g_n + 1;
    mtx_unlock(g_lock);
    i = i + 1;
  }
}

void main() {
  int id;
  int seen;
  g_n = 0;
  g_lock = 1;
  id = thrd_create(worker, 0);
  seen = 0;
  while (seen < 1000) {
    mtx_lock(g_lock);
    seen = g_n;
    mtx_unlock(g_lock);
  }
  thrd_join(id);
  viewport(160, 80);
  text(8, 8, "threads", 0x00FFFFFF);
}
