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

  // https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#locationLink
  // {
  //   "params": {
  //     "version": 0,
  //     "uri": "file%3A%2F%2F%2Fhome%2Fbollu%2Fsoftware%2Fedtr%2Fbuild%2Ftest%2Flake-testdir%2FMain.lean",
  //     "diagnostics": [
  //       {
  //         "source": "Lean 4",
  //         "severity": 1,
  //         "range": { "start": { "line": 0, "character": 0 }, "end": { "line": 0, "character": 0 } },
  //         "message": "unknown package 'LakeTestdir'",
  //         "fullRange": { "start": { "line": 0, "character": 0 }, "end": { "line": 0, "character": 0 }
  //         }
  //       }
  //     ]
  //   },
  //   "method": "textDocument/publishDiagnostics",
  //   "jsonrpc": "2.0"
  // }
  for(json_object_ptr o : state->unhandled_server_requests) {
    fprintf(stderr, "    '%s'\n", json_object_to_json_string_ext(o, JSON_C_TO_STRING_NOSLASHESCAPE));
    json_object *methodo = NULL;
    json_object_object_get_ex(o, "method", &methodo);
    assert(methodo);
    const char *method = json_object_get_string(methodo);

    json_object *paramso =  NULL;
    json_object_object_get_ex(o, "params", &paramso);
    assert(paramso);

    if (!strcmp(method, "textDocument/publishDiagnostics")) {
      json_object *ds = NULL;
      json_object_object_get_ex(paramso, "diagnostics", &ds);
      assert(ds);

      json_object *versiono = NULL;
      json_object_object_get_ex(paramso, "version", &versiono);      
      assert(versiono);
      const int version = json_object_get_int(versiono);

      for(int i = 0; i < json_object_array_length(ds); ++i) {
        json_object *di = json_object_array_get_idx(ds, i);
        LspDiagnostic d = json_parse_lsp_diagnostic(di, version);

        fprintf(stderr, "\t$ PARSED: i: %d, version: %d, range: [(%d, %d)-(%d, %d)], message: \"%s\", severity: %d\n",
            i + 1,
            d.version,
            d.range.start.row,
            d.range.start.col,
            d.range.end.row,
            d.range.end.col,
            d.message.c_str(),
            d.severity);
      }
    } 
  }
  state->unhandled_server_requests.clear();

}
int main() {
  static const int BUF_SIZE = 4096;
  char BUF[BUF_SIZE];
  int nread = 0;
  json_object *req = NULL; json_object_ptr response;
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
  item.version = 42; // check that this version is also used for notifications.
  req = lspCreateDidOpenTextDocumentNotifiation(item);
  fprintf(stderr, "### notification [textDocument/didOpen]\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string_ext(req, JSON_C_TO_STRING_NOSLASHESCAPE));
  state.write_notification_to_child_blocking("textDocument/didOpen", req);

  print_unhandled_requests(&state);

  // $/lean/plainGoal
  Uri uri;
  uri.init_from_file_path(strdup(file_path));
  req = lspCreateLeanPlainGoalRequest(uri, LspPosition(4, 2));
  fprintf(stderr, "### writing $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(req));
  request_id = state.write_request_to_child_blocking("$/lean/plainGoal", req);

  response = state.read_json_response_from_child_blocking(request_id);
  fprintf(stderr, "### [response] $/lean/plainGoal\n");
  fprintf(stderr, " '%s'\n", json_object_to_json_string(response));
  print_unhandled_requests(&state);

  return 0;
};


