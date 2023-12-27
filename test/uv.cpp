// Playing around with libuv.
#include <uv.h>
#include <stdio.h>

void test1_wait_for_a_while(uv_idle_t * handle) {
  static int counter = 0;
  counter++;
  if (counter > 1e6) {
    uv_idle_stop(handle);
  }
}

int test1() {
  uv_idle_t idler;

  uv_idle_init(uv_default_loop(), &idler);
  uv_idle_start(&idler, test1_wait_for_a_while);
  printf("### test1 Idling...\n");
  uv_run(uv_default_loop(), UV_RUN_DEFAULT);
  uv_loop_close(uv_default_loop());
  return 0;
}

int main() { 
  test1();
}
