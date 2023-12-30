#include "lean_lsp.h"
#include "lib.h"
#include <string.h>
#include <stdlib.h>

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#lifeCycleMessages
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initialize
json_object *lspCreateInitializeRequest() {
  // processId
  json_object *o = json_object_new_object();
  json_object_object_add(o, "processId", json_object_new_int(getpid()));
    
  const int CWD_BUF_SIZE = 4096;
  char CWD[CWD_BUF_SIZE];
  if (getcwd(CWD, CWD_BUF_SIZE) == NULL) {
      perror("unable to get cwd.");
  }

  json_object *clientInfo = json_object_new_object();
  json_object_object_add(clientInfo, "name", json_object_new_string("edtr"));
  json_object_object_add(clientInfo, "version", json_object_new_string(VERSION));

  json_object_object_add(o, "clientInfo", clientInfo);

  // json_object_object_add(o, "rootUri", json_object_new_string(CWD));
  json_object_object_add(o, "rootUri", json_object_new_null());
  json_object_object_add(o, "capabilities", lspCreateClientCapabilities());
  return o;
};





void TextDocumentItem::init_from_file_path(fs::path file_path) {
  FILE *fp = NULL;
  if ((fp = fopen(file_path.c_str(), "r")) == NULL) {
      die("unable to create file from path '%s'.", file_path);
  }

  fseek(fp, 0, SEEK_END);
  int file_len = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char *text = (char *)calloc(sizeof(char), file_len + 1);
  int nread = fread(text, 1, file_len, fp);
  assert(nread == file_len && "unable to read file");
  fclose(fp);

  this->uri.init_from_file_path(file_path);
  this->languageId = strdup("lean");
  this->version = 0;
  this->text = text;
  this->is_initialized = true;
}