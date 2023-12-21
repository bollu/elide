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
      
  // initialize/
  req = lspCreateInitializeRequest();
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("initialize", req);
  sleep(1);

  // initialize: got response.
  fprintf(stderr, "PARENT: reading response[initialize]...\n");
  response = state.read_json_response_from_child_blocking();
  fprintf(stderr, "PARENT: response[initialize]: '%s'\n", json_object_to_json_string(response));

  // initialize: send initialized
  req = lspCreateInitializedNotification();
  fprintf(stderr, "PARENT: writing initialized '%s'\n", json_object_to_json_string(req));
  state.write_notification_to_child_blocking("initialized", req);
  sleep(1);

  // textDocument/didOpen
  const char *file_path = "/home/bollu/software/edtr/test/test_file.lean";
  req = lspCreateDidOpenTextDocumentNotifiation(TextDocumentItem::create_from_file_path(file_path));
  fprintf(stderr, "PARENT: writing textDocument/didOpen '%s'\n", json_object_to_json_string_ext(req, JSON_C_TO_STRING_NOSLASHESCAPE));
  state.write_notification_to_child_blocking("textDocument/didOpen", req);
  sleep(1);

  // $/lean/plainGoal
  req = lspCreateLeanPlainGoalRequest(Uri::from_file_path(file_path), Position(0, 35));
  fprintf(stderr, "PARENT: writing $/lean/plainGoal '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("$/lean/plainGoal", req);
  sleep(3);


  nread = state.read_stdout_str_from_child(BUF, BUF_SIZE);
  assert(nread < BUF_SIZE);
  BUF[nread] = 0;
  fprintf(stderr, "PARENT: response 2: '%s'\n",  BUF);

  // response = state.read_json_response_from_child_blocking();
  // fprintf(stderr, "PARENT: response 2: '%s'\n",   json_object_to_json_string(response));

  return 0;
};


