#include <lib.h>

const char *strs[] = {"$", "£",  "ह",  "𐍈"};

void test1() {
  printf("### testing [utf8 string lengths]\n");
  // test utf-8 encoding lengths.
  for(int i = 0; i < 3; ++i) {
    printf("  %s:%d\n", strs[i], (int)strlen(strs[i]));
    assert(strlen(strs[i]) == i + 1);
  }
};

// add up [1..n]
int sum_n(int n) {
  int sum = 0; for(int i = 1; i <= n; ++i) { sum += i; } return sum;
}

void test2() {
  printf("### testing [utf8_next_code_point_len]\n");
  char buf[] = "$£ह𐍈";
  const int len = strlen(buf);
  for(int len = 1; len <= 4; ++len) {
    int sum_prev = sum_n(len - 1);
    const char *p = buf + sum_prev; // move to correct location.
    int out = utf8_next_code_point_len(p);
    printf("  utf8_next_code_point_len '%s' = %d [expected: %d]\n", p, out, len);
    assert(out == len);
  }
}

void test3() {
  printf("### testing [utf8_prev_code_point_len]\n");
  char buf[] = "$£ह𐍈";
  const int len = strlen(buf);
  for(int len = 1; len <= 4; ++len) {
    assert(len >= 1);
    int sum = sum_n(len);
    const int ix = sum - 1;
    printf("  utf8_prev_code_point_len '%s'@%d ", buf, ix);
    printf(" binary bytes '");
    for(int i = sum_n(len - 1); i <= ix; ++i) {
        for(int j = 7; j >= 0; j--)    {
          printf("%d", (int)!!(buf[i] & (1 << j)));
        }
        if (i != ix) { printf(" "); }
    }
    printf("'");
    printf("\n");
    fflush(stdout);
    int out = utf8_prev_code_point_len(buf, ix);
    printf("    = %d [expected: %d]\n", out, len);
    assert(out == len);
  }
}

int main() {
  test1();
  test2();
  test3();
  return 0;
}

