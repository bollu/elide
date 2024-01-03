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
  json_object *req = NULL;
  json_object_ptr response;
  LspRequestId request_id(-1);

  LeanServerState state = LeanServerState::init({});

  fprintf(stderr, "### reading child (stderr), expecting 'starting lean --server'...\n");
  nread = state._read_stderr_str_from_child_blocking();
  fprintf(stderr, " response: '%s'.\n",
    state.child_stderr_buffer.to_string_len(nread));
  state.child_stderr_buffer.dropNBytesMut(nread); 
      
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
  const char *file_path = "test_file.lean";
  TextDocumentItem item; item.init_from_file_path(file_path);
  req = lspCreateDidOpenTextDocumentNotifiation(item);
  fprintf(stderr, "### notification [textDocument/didOpen]\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string_ext(req, JSON_C_TO_STRING_NOSLASHESCAPE));
  state.write_notification_to_child_blocking("textDocument/didOpen", req);

  // textDocument/hover
  Uri uri; uri.init_from_file_path(strdup(file_path));
  req = lspCreateTextDocumentHoverRequest(uri, Position(0, 35));
  fprintf(stderr, "### writing textDocument/hover\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("textDocument/hover", req);

  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "### [response] textDocument/hover\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
  return 0;
};
