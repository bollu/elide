#include "lib.h"
#include "lean_lsp.h"


// TODO: think about how to make all of this non-blocking.
int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;


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
  json_object *req = lspCreateInitializeRequest();
  fprintf(stderr, "PARENT: writing '%s'\n", json_object_to_json_string(req));
  state.write_request_to_child_blocking("initialize", req);
  fprintf(stderr, "PARENT: sleeping...\n");
  sleep(3);

  // fprintf(stderr, "PARENT: reading child (stderr)...\n");
  // nread = state.read_stderr_from_child(BUF, BUF_SIZE);
  // BUF[nread] = 0;
  // fprintf(stderr, "PARENT: child response (stderr): '%s'.\n", BUF);
  // sleep(1);
  
  sleep(1);
  fprintf(stderr, "PARENT: reading child (stdout)...\n");
  json_object *response1 = state.read_response_from_child_blocking();
  fprintf(stderr, "PARENT: child response 1: '%s'\n",
    json_object_to_json_string(response1));
  fprintf(stderr, "PARENT: quitting...\n");
  exit(0);

  return 0;
};

