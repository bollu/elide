#include "lib.h"

void test1() {
  printf("@@@ test1: checking rg JSON output.\n");
  RgProcess process;
  std::vector<std::string> args;
  args.push_back("quux");
  args.push_back("rg.txt");
  args.push_back("--json");
  printf("### executing rg.\n");
  process.execpAsync(".", args);
  printf("### sleeping...\n");
  printf("### readling lines from rg...\n");
  int total_lines = 0;
  for(int i = 0; i < 5; ++i) {
    printf("  reading [iter=#%d]...", i);
    const int nlines = process.readLinesNonBlocking();
    total_lines += nlines;
    printf(" | read new lines [nlines=%d]\n", nlines);
    for(abuf line : process.lines) {
      printf("    . line: '%s'\n", line.to_string());
    }
    process.lines.clear();
    if (!process.isRunningNonBlocking()) {
      printf("    . process has TERMINATED. Stopping loop!\n");
      break;
    }
    sleep(1);
  }
  printf("### final buffer\n");
  printf("  '%s'\n", process.child_stdout_buffer.to_string());
  printf("### killing rg...\n");
  process.killSync();
  printf("### Total [nlines=%d]\n", total_lines);
  assert(total_lines == 4);
}


void test2() {
  printf("@@@ test2: checking rg file output.\n");
  RgProcess process;
  std::vector<std::string> args;
  args.push_back("--files");
  args.push_back("-g");
  args.push_back("rg.txt");
  printf("### executing rg.\n");
  process.execpAsync(".", args);
  printf("### sleeping...\n");
  printf("### readling lines from rg...\n");
  int total_lines = 0;
  for(int i = 0; i < 5; ++i) {
    printf("  reading [iter=#%d]...", i);
    const int nlines = process.readLinesNonBlocking();
    total_lines += nlines;
    printf(" | read new lines [nlines=%d]\n", nlines);
    for(abuf line : process.lines) {
      printf("    . line: '%s'\n", line.to_string());
    }
    process.lines.clear();
    if (!process.isRunningNonBlocking()) {
      printf("    . process has TERMINATED. Stopping loop!\n");
      break;
    }
    sleep(1);
  }
  printf("### final buffer\n");
  printf("  '%s'\n", process.child_stdout_buffer.to_string());
  printf("### killing rg...\n");
  process.killSync();
  printf("### Total [nlines=%d]\n", total_lines);
  assert(total_lines == 1);
}
int main() {
  test1();
  test2();
  return 0;
}

