// Playing around with libuv.
#include <libuv/libuv.h>
int test1() {
  uv_loop_t loop;
  uv_loop_init(loop);
}

int main() { 
  test1();
}
