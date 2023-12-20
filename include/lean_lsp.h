// APIs to create objects for the lean LSP
#include <json-c/json.h>
#include "uri_encode.h"


// == toplevel capabilities ==
// structure ClientCapabilities where
//   textDocument? : Option TextDocumentClientCapabilities := none
//   window?       : Option WindowClientCapabilities       := none
//   workspace?    : Option WorkspaceClientCapabilities    := none
// deriving ToJson, FromJson

// == workspace capabilities ==
// structure WorkspaceEditClientCapabilities where
//   /-- The client supports versioned document changes in `WorkspaceEdit`s. -/
//   documentChanges?         : Option Bool := none
//   /--  Whether the client in general supports change annotations on text edits. -/
//   changeAnnotationSupport? : Option ChangeAnnotationSupport := none
//   /-- The resource operations the client supports. Clients should at least support 'create', 'rename' and 'delete' files and folders. -/
//   resourceOperations?      : Option (Array String) := none
//   deriving ToJson, FromJson
// src/lean/Lean/Data/Lsp/Capabilities.lean
// structure WorkspaceClientCapabilities where
//   applyEdit: Bool
//   workspaceEdit? : Option WorkspaceEditClientCapabilities := none
//   deriving ToJson, FromJson
// == text document capabilities ==
// structure CompletionItemCapabilities where
//   insertReplaceSupport? : Option Bool := none
//   deriving ToJson, FromJson

// structure CompletionClientCapabilities where
//   completionItem? : Option CompletionItemCapabilities := none
//   deriving ToJson, FromJson

// structure TextDocumentClientCapabilities where
//   completion? : Option CompletionClientCapabilities := none
//   codeAction? : Option CodeActionClientCapabilities := none
//   deriving ToJson, FromJson

// == window capabilities.
// structure ShowDocumentClientCapabilities where
//   support : Bool
//   deriving ToJson, FromJson

// structure WindowClientCapabilities where
//  showDocument? : Option ShowDocumentClientCapabilities := none
// deriving ToJson, FromJson

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





struct Uri {
  char *uri; // owned by uri;

  Uri(Uri &uri) {
    this->uri = strdup(uri.uri);
  }

  Uri(char *uri) : uri(uri) {}
  ~Uri() { free(uri); }

  static Uri from_file_path(const char *file_path) {
    const char *file_segment_uri = "file://";
    const int file_uri_unencoded_len = strlen(file_segment_uri) + strlen(file_path) + 1;
    char *file_uri_unencoded = (char *)calloc(sizeof(char), file_uri_unencoded_len);
    sprintf(file_uri_unencoded, "%s%s", file_segment_uri, file_path);

    const int file_uri_encoded_len = file_uri_unencoded_len *4 + 1;
    char *out = (char *)calloc(sizeof(char), file_uri_encoded_len); // at most 8 -> 32 blowup.
    uri_encode(file_uri_unencoded, file_uri_unencoded_len, out);
    free(file_uri_unencoded); // song and dance...
    return Uri(out);
  }


};

json_object *json_object_new_uri(Uri uri) {
  return json_object_new_string(uri.uri);
}


// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentItem
struct TextDocumentItem {
  Uri uri;
  const char *languageId;
  int version; // version of the document. (it will increase after each change, including undo/redo).
  char *text; // text of the document

  TextDocumentItem(Uri uri, const char *languageId, int version, char *text) : 
    uri(uri), languageId(languageId), version(version), text(text) {};
  static TextDocumentItem create_from_file_path(const char *file_path);
};


TextDocumentItem TextDocumentItem::create_from_file_path(const char *file_path) {

  FILE *fp = NULL;
  if ((fp = fopen(file_path, "r")) == NULL) {
      die("unable to create file from path.");
  }

  fseek(fp, 0, SEEK_END);
  int file_len = ftell(fp);
  fseek(fp, 0, SEEK_SET);

  char *text = (char *)calloc(sizeof(char), file_len + 1);
  int nread = fread(text, 1, file_len, fp);
  assert(nread == file_len && "unable to read file");
  fclose(fp);

  return TextDocumentItem(Uri::from_file_path(file_path), "lean", 0, text);
}


json_object *json_object_new_text_document_item(TextDocumentItem item) {
  json_object *o = json_object_new_object();
  json_object_object_add(o, "uri", json_object_new_uri(item.uri));
  json_object_object_add(o, "languageId", json_object_new_string(item.languageId));
  json_object_object_add(o, "version", json_object_new_int(item.version));
  json_object_object_add(o, "text", json_object_new_string(item.text));
  return o;
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didOpen
json_object *lspCreateDidOpenTextDocumentRequest(TextDocumentItem item) {
  json_object *o = json_object_new_object();
  json_object_object_add(o, "textDocument", json_object_new_text_document_item(item));
  return o;
};


// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didChange
json_object *lspCreateDidChangeTextDocumentRequest(TextDocumentItem item) {
  json_object *o = json_object_new_object();
  // VersionedTextDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(item.uri));
  json_object_object_add(textDocument, "version", json_object_new_int(item.version));

  json_object_object_add(o, "textDocument", textDocument);

  json_object *contentChanges = json_object_new_array();
  
  json_object *contentChangeEvent = json_object_new_object();
  json_object_object_add(contentChangeEvent, "text", json_object_new_string(item.text));

  json_object_array_add(contentChanges, contentChangeEvent);
  json_object_object_add(o, "contentChanges", contentChanges);
  return o;
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didChange
json_object *lspCreateDidCloseTextDocumentRequest(const char *uri) {

  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(strdup(uri)));
  json_object_object_add(o, "textDocument", textDocument);
  return o;
}


struct Position {
  int row = -42; // zero indexed line number.
  int col = -42; // utf-8 offset for column.

  Position(int row, int col) : row(row), col(col) {};
};

json_object *json_object_new_position(Position position) {
  json_object *o  = json_object_new_object();
  json_object_object_add(o, "line", json_object_new_int64(position.row));
  json_object_object_add(o, "character", json_object_new_int64(position.col));
  return o;
}

// $/lean/plainTermGoal
json_object *lspCreateLeanPlainTermGoalRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};


// $/lean/plainGoal
json_object *lspCreateLeanPlainGoalRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};


// struct PlainTermGoalResponse {};

// struct PlainGoalResponse {};