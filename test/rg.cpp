#include "lib.h"

void test1() {
  printf("━━━━━test1: checking rg JSON output━━━━━\n");
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
  printf("━━━━━test2: checking rg file output━━━━━\n");
  RgProcess process;
  std::vector<std::string> args;
  args.push_back("--files");
  args.push_back("-g");
  args.push_back("*");
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
      // check that we in fact have lines.
      for(Ix<Byte> i(0); i < line.len(); ++i) {
          assert(line.getByteAt(i) != '\n');
          assert(line.getByteAt(i) != '\r');
      }
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
}


void test3() {
  printf("━━━━━test3: CtrlPView::RgArgs creation━━━━━\n");
  abuf buf = abuf::from_copy_str("rg.txt");
  printf("### testing command '%s'\n", buf.to_string());
  CtrlPView::RgArgs args = CtrlPView::parseUserCommand(buf);
  assert(args.filePattern  == "rg.txt");
  assert(args.dirPatterns.size() == 0);
  assert(args.searchPatterns.size() == 0);

  std::vector<std::string> argsVec = CtrlPView::rgArgsToCommandLineArgs(args);  
  printf("### args vec[size=%d]\n", (int)argsVec.size());
  for(int i = 0; i < argsVec.size(); ++i) {
    printf("  . %s\n", argsVec[i].c_str());
  }  
  assert(argsVec.size() == 3);
  assert(argsVec[0] == "--files");
  assert(argsVec[1] == "-g");
  assert(argsVec[2] == "rg.txt");
}


int main() {
  test1();
  test2();
  test3();
  return 0;
}

