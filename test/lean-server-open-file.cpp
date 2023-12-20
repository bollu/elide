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
      
  // initialize
  req = lspCreateInitializeRequest();
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("initialize", req);
  sleep(1);
  fprintf(stderr, "PARENT: reading child (stdout)...\n");
  response = state.read_json_response_from_child_blocking();
  fprintf(stderr, "PARENT: response 1: '%s'\n",
  json_object_to_json_string(response));

  // textDocument/didOpen
  const char *file_path = "/home/bollu/software/edtr/test/test_file.lean";
  req = lspCreateDidOpenTextDocumentRequest(TextDocumentItem::create_from_file_path(file_path));
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("textDocument/didOpen", req);
  sleep(1);
  state.read_empty_response_from_child_blocking();

  {   
    const int BUFSIZE = 1000000;
    char *buf = (char *)calloc(BUFSIZE, sizeof(char));
    int nread = state.read_stdout_str_from_child(buf, BUFSIZE);
    assert(nread < BUFSIZE);
    buf[nread] = 0;
    fprintf(stderr, "READ: '%s'\n", buf);

  }
  
  // $/lean/plainGoal
  req = lspCreateLeanPlainGoalRequest(Uri::from_file_path(file_path), Position(0, 0));
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("$/lean/plainGoal", req);
  sleep(1);
  // response = state.read_json_response_from_child_blocking();
  // fprintf(stderr, "PARENT: response 2: '%s'\n",
  // json_object_to_json_string(response));

  return 0;
};


