#include "lib.h"
#include "lean_lsp.h"


// TODO: think about how to make all of this non-blocking.
int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  LspRequestId request_id(-1);
  json_object *response = NULL;

  enableRawMode();
  LeanServerState state = LeanServerState::init(LeanServerInitKind::LST_LEAN_SERVER);

  fprintf(stderr, "### reading child [stderr], expecting 'starting lean --server'...\n");
  nread = state._read_stderr_str_from_child_blocking();
  fprintf(stderr, "   child response [stderr]: '%s'.\n",
    state.child_stderr_buffer.to_string_len(nread));
  state.child_stderr_buffer.drop_prefix(nread); 

  fprintf(stderr, "### request [initialize]\n");
  json_object *req = lspCreateInitializeRequest();
  fprintf(stderr, "     writing '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("initialize", req);

  fprintf(stderr, "### response [initialize]...\n");
  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "    child response: '%s'\n",
    json_object_to_json_string(response));

  // initialize: send initialized
  req = lspCreateInitializedNotification();
  fprintf(stderr, "### writing [initialized] '%s'\n", json_object_to_json_string(req));
  state.write_notification_to_child_blocking("initialized", req);

  fprintf(stderr, "### quitting...\n");
  exit(0);

  return 0;
};

