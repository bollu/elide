#include "lib.h"
#include "lean_lsp.h"

#ifdef __cplusplus
extern "C"
#endif
const char* __asan_default_options() { return "detect_leaks=0"; }

void print_unhandled_requests(LeanServerState *state) {
  fprintf(stderr, "  #### unhandled responses\n");  
  if (state->unhandled_server_requests.size() == 0) {
    fprintf(stderr, "    all handled.\n");
    return;
  }

  for(json_object *o : state->unhandled_server_requests) {
    fprintf(stderr, "    '%s'\n", json_object_to_json_string_ext(o, JSON_C_TO_STRING_NOSLASHESCAPE));
  }
  state->unhandled_server_requests.clear();

}
int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  json_object *req = NULL, *response = NULL;
  LspRequestId request_id(-1);

  const char *file_path = realpath("./lake-testdir/Main.lean", NULL);
  LeanServerState state = LeanServerState::init(file_path);

  // lake is silent, does not write anything to stderr.
  // fprintf(stderr, "### reading child (stderr), expecting 'starting lean --server'...\n");
  // nread = state._read_stderr_str_from_child_blocking();
  // fprintf(stderr, " response: '%s'.\n",
  //   state.child_stderr_buffer.to_string_len(nread));
  // state.child_stderr_buffer.drop_prefix(nread); 
      
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

  TextDocumentItem item; item.init_from_file_path(file_path);
  req = lspCreateDidOpenTextDocumentNotifiation(item);
  fprintf(stderr, "### notification [textDocument/didOpen]\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string_ext(req, JSON_C_TO_STRING_NOSLASHESCAPE));
  state.write_notification_to_child_blocking("textDocument/didOpen", req);

  print_unhandled_requests(&state);

  // $/lean/plainGoal
  Uri uri;
  uri.init_from_file_path(strdup(file_path));
  req = lspCreateLeanPlainGoalRequest(uri, Position(4, 2));
  fprintf(stderr, "### writing $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("$/lean/plainGoal", req);

  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "### [response] $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
  print_unhandled_requests(&state);

  return 0;
};


