#include "lib.h"
#include "lean_lsp.h"



int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  json_object *req = NULL, *response = NULL;

  enableRawMode();
  LeanServerState state = LeanServerState::init(LeanServerInitKind::LST_LEAN_SERVER);

  fprintf(stderr, "PARENT: reading child (stderr), expecting 'starting lean --server'...\n");
  nread = state.read_stderr_str_from_child(BUF, BUF_SIZE);
  BUF[nread] = 0;
  fprintf(stderr, "PARENT: child response (stderr): '%s'.\n", BUF);
  sleep(1);
  fprintf(stderr, "PARENT: sleeping...\n");
  sleep(1);
  fprintf(stderr, "PARENT: writing req into buffer\n");
    
  req = lspCreateInitializeRequest();
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("initialize", req);
  sleep(1);
  fprintf(stderr, "PARENT: reading child (stdout)...\n");
  response = state.read_json_response_from_child_blocking();
  fprintf(stderr, "PARENT: response 1: '%s'\n",
  json_object_to_json_string(response));


  const char *file_path = "/home/bollu/software/edtr/test/test_file.lean";
  req = lspCreateDidOpenTextDocumentRequest(TextDocumentItem::create_from_file_path(file_path));
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("textDocument/didOpen", req);
  sleep(1);
  state.read_empty_response_from_child_blocking();

  return 0;
};


