#include "lib.h"
#include "lean_lsp.h"

#ifdef __cplusplus
extern "C"
#endif
const char* __asan_default_options() { return "detect_leaks=0"; }


// TODO: think about how to make all of this non-blocking.
int main() {
  // for(int i = 0; i < 2; ++i)  {
    // if (i == 0) { enableRawMode(); } 
    // else { disableRawMode(); }

    static const int BUF_SIZE = 4096;
    char BUF[BUF_SIZE];
    int nread = 0;
    LspRequestId request_id(-1);
    json_object_ptr response;

    LeanServerState state = LeanServerState::init({});

    fprintf(stderr, "### reading child [stderr], expecting 'starting lean --server'...\n");
    nread = state._read_stderr_str_from_child_blocking();
    fprintf(stderr, "   child response [stderr]: '%s'.\n",
      state.child_stderr_buffer.to_string_len(nread));
    state.child_stderr_buffer.dropNBytesMut(nread); 

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
  // }

  fprintf(stderr, "### quitting...\n");
  exit(0);

  return 0;
};

