#include "lib.h"
#include "lean_lsp.h"

int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  json_object *req = NULL, *response = NULL;
  LspRequestId request_id(-1);

  enableRawMode();
  LeanServerState state = LeanServerState::init(LeanServerInitKind::LST_LEAN_SERVER);

  fprintf(stderr, "### reading child (stderr), expecting 'starting lean --server'...\n");
  nread = state._read_stderr_str_from_child_blocking();
  fprintf(stderr, " response: '%s'.\n",
    state.child_stderr_buffer.to_string_len(nread));
  state.child_stderr_buffer.drop_prefix(nread); 
      
  // initialize/
  req = lspCreateInitializeRequest();
  fprintf(stderr, "### request [initialize]\n");
  fprintf(stderr, "  '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("initialize", req);

  // initialize: got response.
  fprintf(stderr, "### reading response[initialize]\n");
  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "  '%s'\n", json_object_to_json_string(response));

  // initialize: send initialized
  req = lspCreateInitializedNotification();
  fprintf(stderr, "### notification [initialized]\n");
  fprintf(stderr," '%s'\n", json_object_to_json_string(req));
  state.write_notification_to_child_blocking("initialized", req);

  // textDocument/didOpen
  const char *file_path = "/home/bollu/software/edtr/test/test_file.lean";
  req = lspCreateDidOpenTextDocumentNotifiation(TextDocumentItem::create_from_file_path(file_path));
  fprintf(stderr, "### notification [textDocument/didOpen]\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string_ext(req, JSON_C_TO_STRING_NOSLASHESCAPE));
  state.write_notification_to_child_blocking("textDocument/didOpen", req);

  // $/lean/plainGoal
  req = lspCreateLeanPlainGoalRequest(Uri::from_file_path(file_path), Position(0, 35));
  fprintf(stderr, "### writing $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("$/lean/plainGoal", req);


  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "### [response] $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
  return 0;
};


