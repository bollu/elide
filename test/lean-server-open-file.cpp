#include "lib.h"

json_object *lspCreateClientCapabilities() {
  json_object *o = json_object_new_object();
  return o;  
}


// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#lifeCycleMessages
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initialize
json_object *lspCreateInitializeRequest() {
  // processId
  json_object *o = json_object_new_object();
  json_object_object_add(o, "processId", json_object_new_int(getpid()));
    
  const int CWD_BUF_SIZE = 4096;
  char CWD[CWD_BUF_SIZE];
  if (getcwd(CWD, CWD_BUF_SIZE) == NULL) {
      perror("unable to get cwd.");
  }

  json_object *clientInfo = json_object_new_object();
  json_object_object_add(clientInfo, "name", json_object_new_string("edtr"));
  json_object_object_add(clientInfo, "version", json_object_new_string(VERSION));

  json_object_object_add(o, "clientInfo", clientInfo);

  // json_object_object_add(o, "rootUri", json_object_new_string(CWD));
  json_object_object_add(o, "rootUri", json_object_new_null());
  json_object_object_add(o, "capabilities", lspCreateClientCapabilities());
  return o;
};

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
  // json_object *response2 = state.read_response_from_child_blocking();
  // nread = state.read_stdout_str_from_child(BUF, BUF_SIZE);
  // BUF[nread] = 0;
  // fprintf(stderr, "PARENT: child response (stdout): '%s'\n", BUF);
  fprintf(stderr, "PARENT: quitting...\n");
  exit(1);

  return 0;
};


