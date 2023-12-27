#include "lib.h"


// test creating a newline correctly indents the line.
void test1() {
   FileConfig f;
   abuf buf;

   f.rows.push_back(FileRow());
   const char beforeBytes[] = "  twospaces";
   printf("### testing creating newline at '  two|spaces'\n");
   f.rows[0].setBytes(beforeBytes, strlen(beforeBytes), f);
   f.cursor.row = 0;
   f.cursor.col = Size<Codepoint>(5);

   buf = abuf();
   fileConfigDebugPrint(&f, &buf);
   printf("vvv---buffer state before---\n");
   char *beforeStr = buf.to_string();
   puts(beforeStr);
   printf("^^^\n");
   free(beforeStr);

   fileConfigInsertNewline(&f);

   buf = abuf();   
   fileConfigDebugPrint(&f, &buf);
   printf("vvv---buffer state after--\n");
   char *afterStr = buf.to_string();
   puts(afterStr);
   printf("^^^\n");

   assert(strcmp(afterStr, "  two\n  |spaces") == 0);
   free(afterStr);

}

int main() {
  test1();
}

