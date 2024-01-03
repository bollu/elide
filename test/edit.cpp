#include "lib.h"
#include <vector>
#include <string>

void printFileConfig(FileConfig *f, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int ERROR_LEN = 9000; 
  char *title_str = (char*)malloc(sizeof(char) * ERROR_LEN);
  vsnprintf(title_str, ERROR_LEN, fmt, args);
  va_end(args);

  abuf buf;
  fileConfigDebugPrint(f, &buf);
  
  printf("┌─[%10s]──────────┐\n", title_str);
  char *str = buf.to_string();
  for(int i = 0; i < strlen(str); ++i) {
    if (str[i] == ' ') { str[i] = '~'; } // change to '~' to read more clearly.
  }
  puts(str);
  printf("└───────────────────────┘\n");
  free(str);
  free(title_str);
}

// set the test string to init the file config.
void fileConfigSetTestString(FileConfig &f, const char *s) {
  std::vector<std::string> lines;
  lines.push_back(std::string());

  int row = 0, col = 0, cursor_row = -1, cursor_col = -1;
  for(int i = 0; i < strlen(s); ++i, ++col) {

    assert((s[i] >= 'a' && s[i] <= 'z') || 
           (s[i] >= '0' && s[i] <= '9') ||
    	    s[i] == ' ' || s[i] == '\n' || s[i] == '\t' || s[i] == '|');

    if (s[i] == '\n') {
      row++;
      col = 0;
      lines.push_back(std::string());
    } else if (s[i] == '|') {
      cursor_row = row; cursor_col = col;
    } else {
      lines[row].push_back(s[i]);
    }
  }

  assert(cursor_row != -1);
  assert(cursor_col != -1);

  f.cursor.row = cursor_row;
  f.cursor.col = Size<Codepoint>(cursor_col);

  for(const std::string &s : lines) {
     abuf row;
     row.setBytes(s.c_str(), s.size());
     f.rows.push_back(row);
  }
};


struct Test {
  const char *before = nullptr; // initial state.
  const char *expected = nullptr; // expected state.
  using Fn = std::function<void(FileConfig *)>;

  Fn runner;
  Test(const char *before, const char *expected, Test::Fn runner) :
    before(before), expected(expected), runner(runner) {};

  bool run() {
    static int ntest = 1;
    printf("@@@ Test %d\n", ntest++);
    FileConfig f(FileLocation("", Cursor(0, 0)));
    fileConfigSetTestString(f, this->before);
    printFileConfig(&f, "Before");
    this->runner(&f);

    abuf buf;
    fileConfigDebugPrint(&f, &buf);
    char *afterStr = buf.to_string();
    printFileConfig(&f, "After");

    const bool success = strcmp(afterStr, this->expected) == 0;
    if (!success) {
      printf("┌─[%10s]──────────┐\n", "Expected");
      for(int i = 0; i < strlen(this->expected); ++i) {
	char c = this->expected[i];
        putc(c == ' ' ? '~' : c, stdout);
      }
      printf("\n");
      printf("└───────────────────────┘\n");
    }
    printf("━━━━━━━━━━[%2s]━━━━━━━━━━━━\n", success ? "✓" : "✗");

    free(afterStr);
    return success;
  }
};

Test test1("  two|spaces", "  two\n  |spaces", [](FileConfig *f) {
  fileConfigInsertEnterKey(f);
});

Test test2("  test| test2", "  test\n|  test2", [](FileConfig *f) {
  fileConfigInsertEnterKey(f);
});

Test test3("  test|   test2", "  test\n|   test2", [](FileConfig *f) {
  fileConfigInsertEnterKey(f);
});

int main() {
  test1.run();
  test2.run();
  test3.run();
}

