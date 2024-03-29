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

  for(json_object_ptr o : state->unhandled_server_requests) {
    fprintf(stderr, "    '%s'\n", json_object_to_json_string_ext(o, JSON_C_TO_STRING_NOSLASHESCAPE));
  }
  state->unhandled_server_requests.clear();

}
int main(int argc, char **argv) {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  json_object *req = NULL; json_object_ptr response;
  LspRequestId request_id(-1);

  char *file_path = NULL;
  if (argc >= 2) {
    file_path = realpath(argv[1], NULL);
  } else {
    file_path = realpath("./lake-testdir/Main.lean", NULL);
  }
  fprintf(stderr, "file_path: %s\n", file_path);
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
  assert(state.unhandled_server_requests.size() == 0);
  
  // $/lean/plainGoal
  Uri uri;
  uri.init_from_file_path(strdup(file_path));
  LspPosition pos = LspPosition(4, 2);
  if (argc >= 3) {
    sscanf(argv[2], "%d:%d", &pos.row, &pos.col);
  }
  req = lspCreateLeanPlainGoalRequest(uri, pos);
  fprintf(stderr, "### writing $/lean/plainGoal @ (%d, %d)\n", pos.row, pos.col);
  fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("$/lean/plainGoal", req);

  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "### [response] $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
  print_unhandled_requests(&state);


  for(int i = 3; i < argc; ++i) {
    assert(state.unhandled_server_requests.size() == 0);
    LspPosition pos(0, 0);
    sscanf(argv[i], "%d:%d", &pos.row, &pos.col);
    req = lspCreateLeanPlainGoalRequest(uri, pos);
    fprintf(stderr, "### writing $/lean/plainGoal @ (%d, %d)\n", pos.row, pos.col);
    fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
    request_id = state.write_request_to_child_blocking("$/lean/plainGoal", req);

    response = state.read_json_response_from_child_blocking(request_id);
    fprintf(stderr, "### [response] $/lean/plainGoal\n");
    fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
    print_unhandled_requests(&state);
  }

  return 0;
};


