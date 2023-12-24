#include "lib.h"
#include "lean_lsp.h"

#ifdef __cplusplus
extern "C"
#endif
const char* __asan_default_options() { return "detect_leaks=0"; }


int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  json_object *req = NULL, *response = NULL;
  LspRequestId request_id(-1);

  LeanServerState state = LeanServerState::init(NULL);

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
  const char *file_path = "/home/bollu/software/edtr/test/test_file.txt";
  TextDocumentItem item; item.init_from_file_path(file_path);
  req = lspCreateDidOpenTextDocumentNotifiation(item);
  fprintf(stderr, "### notification [textDocument/didOpen]\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string_ext(req, JSON_C_TO_STRING_NOSLASHESCAPE));
  state.write_notification_to_child_blocking("textDocument/didOpen", req);

  // $/lean/plainGoal
  Uri uri; uri.init_from_file_path(strdup(file_path));
  req = lspCreateLeanPlainGoalRequest(uri, Position(0, 35));
  fprintf(stderr, "### writing $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("$/lean/plainGoal", req);

  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "### [response] $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
  return 0;
};

