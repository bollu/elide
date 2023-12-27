#include "lib.h"


void printFile(FileConfig *f, const char *fmt, ...) {
  va_list args;
  va_start(args, fmt);
  const int ERROR_LEN = 9000; 
  char *title_str = (char*)malloc(sizeof(char) * ERROR_LEN);
  vsnprintf(title_str, ERROR_LEN, fmt, args);
  va_end(args);

  abuf buf;
  fileConfigDebugPrint(f, &buf);
  
  printf("vvvvvv---[%s]---\n", title_str);
  char *beforeStr = buf.to_string();
  puts(beforeStr);
  printf("^^^^^^\n");
  free(beforeStr);
  free(title_str);
}

// test creating a newline correctly indents the line.
void test1() {
   FileConfig f;
   abuf buf;
   char *afterStr = NULL;

   f.rows.push_back(FileRow());
   const char beforeBytes[] = "  twospaces";
   printf("### testing creating newline at '  two|spaces'\n");
   f.rows[0].setBytes(beforeBytes, strlen(beforeBytes), f);
   f.cursor.row = 0;
   f.cursor.col = Size<Codepoint>(5);
   printFile(&f, "buffer state before");

   fileConfigInsertEnterKey(&f);
   printFile(&f, "buffer state after");

  fileConfigDebugPrint(&f, &buf);
  afterStr = buf.to_string();
   assert(strcmp(afterStr, "'  two'\n'  |spaces'") == 0);
   free(afterStr);

}

// test cursor motion by word.
void test2() {
  FileConfig f;
  abuf buf;
  char *afterStr = NULL;

  f.rows.push_back(FileRow());
  const char beforeBytes[] = "  aa bbb  \n   cc ddd  ";
  printf("### testing next word motion at\n");
  f.rows[0].setBytes(beforeBytes, strlen(beforeBytes), f);
  f.cursor.row = 0;
  f.cursor.col = Size<Codepoint>(0);
  printFile(&f, "buffer state 0");

  fileConfigInsertEnterKey(&f);
  printFile(&f, "buffer state 1");

  fileConfigDebugPrint(&f, &buf);
  afterStr = buf.to_string();
  assert(strcmp(afterStr, "'  two'\n'  |spaces'") == 0);
  free(afterStr);
}

int main() {
  test1();
  test2();
}

