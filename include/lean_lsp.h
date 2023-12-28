// APIs to create objects for the lean LSP
#pragma once
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <iostream>
#include <iterator>
#include <json-c/json.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>
#include <vector>
#include <unordered_map>
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

static json_object *lspCreateClientCapabilities() {
  json_object *o = json_object_new_object();

  json_object * text_document_capabilities = json_object_new_object();

  json_object_object_add(o, "textDocument", text_document_capabilities);
  return o;  
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#lifeCycleMessages
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initialize
json_object *lspCreateInitializeRequest();


struct Uri {
  char *uri = nullptr; // owned by uri;

  bool is_initialized() const {
    return uri != nullptr;
  }

  Uri() = default;

  Uri(const Uri &uri) {
    this->uri = strdup(uri.uri);
  }

  ~Uri() { free(uri); }

  void init_from_file_path(const char *file_path) {
    const char *file_segment_uri = "file://";

    const int file_uri_unencoded_len = strlen(file_segment_uri) + strlen(file_path) + 1;
    char *file_uri_unencoded = (char *)calloc(sizeof(char), file_uri_unencoded_len);
    sprintf(file_uri_unencoded, "%s%s", file_segment_uri, file_path);

    const int file_uri_encoded_len = file_uri_unencoded_len *4 + 1;
    char *out = (char *)calloc(sizeof(char), file_uri_encoded_len); // at most 8 -> 32 blowup.
    uri_encode(file_uri_unencoded, file_uri_unencoded_len, out);
    free(file_uri_unencoded); // song and dance...
    this->uri = out;
  }


};

static json_object *json_object_new_uri(Uri uri) {
  return json_object_new_string(uri.uri);
}


// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentItem
struct TextDocumentItem {
  Uri uri;
  char *languageId = nullptr;; // owned pointer of the language.
  int version = -1; // version of the document. (it will increase after each change, including undo/redo).
  char *text = nullptr; // owned pointer of the text of the document
  bool is_initialized = false;

  TextDocumentItem() = default;
  TextDocumentItem(Uri uri, char *languageId, int version, char *text) : 
    uri(uri), languageId(languageId), version(version), text(text), is_initialized(true) {};

  TextDocumentItem(const TextDocumentItem &other) : 
  	uri(other.uri), 
	languageId(strdup(other.languageId)),
	version(other.version),
  	text(strdup(other.text)), is_initialized(true) {}
  void init_from_file_path(const char *file_path);

  ~TextDocumentItem() {
    free(text);
    free(languageId);
  }
};


static json_object *json_object_new_text_document_item(TextDocumentItem item) {
  json_object *o = json_object_new_object();
  json_object_object_add(o, "uri", json_object_new_uri(item.uri));
  json_object_object_add(o, "languageId", json_object_new_string(item.languageId));
  json_object_object_add(o, "version", json_object_new_int(item.version));
  json_object_object_add(o, "text", json_object_new_string(item.text));
  return o;
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didOpen
static json_object *lspCreateDidOpenTextDocumentNotifiation(TextDocumentItem item) {
  json_object *o = json_object_new_object();
  json_object_object_add(o, "textDocument", json_object_new_text_document_item(item));
  return o;
};


// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didChange
static json_object *lspCreateDidChangeTextDocumentRequest(TextDocumentItem item) {
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

// TODO:
static json_object *lspCreateInitializedNotification() {
  return json_object_new_object();
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didChange
// static json_object *lspCreateDidCloseTextDocumentRequest(const char *uri) {

//   json_object *o = json_object_new_object();
//   // textDocumentIdentifier
//   json_object *textDocument = json_object_new_object();
//   json_object_object_add(textDocument, "uri", json_object_new_uri(strdup(uri)));
//   json_object_object_add(o, "textDocument", textDocument);
//   return o;
// }


struct Position {
  int row = -42; // zero indexed line number.
  int col = -42; // utf-8 offset for column.

  Position(int row, int col) : row(row), col(col) {};
};

static json_object *json_object_new_position(Position position) {
  json_object *o  = json_object_new_object();
  json_object_object_add(o, "line", json_object_new_int64(position.row));
  json_object_object_add(o, "character", json_object_new_int64(position.col));
  return o;
}

// $/lean/plainTermGoal
static json_object *lspCreateLeanPlainTermGoalRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};


// $/lean/plainGoal
static json_object *lspCreateLeanPlainGoalRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};


// textDocument/hover
static json_object *lspCreateTextDocumentHoverRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};


enum class CompletionTriggerKind {
  Invoked = 1,
  TriggerCharacter = 2,
  TriggerForIncompleteCompletions = 3
};

// textDocument/completion
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_completion
static json_object *lspCreateTextDocumentCompletionRequest(Uri uri, const Position position, CompletionTriggerKind triggerKind) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  json_object *context = json_object_new_object();
  json_object_object_add(context, "triggerKind", json_object_new_int(int(triggerKind)));
  json_object_object_add(o, "context", context);
  return o;
};



// textDocument/definition
static json_object *lspCreateTextDocumentDefinitionRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};


// textDocument/declaration
static json_object *lspCreateTextDocumentDeclarationRequest(Uri uri, const Position position) {
  json_object *o = json_object_new_object();
  // textDocumentIdentifier
  json_object *textDocument = json_object_new_object();
  json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
  json_object_object_add(o, "textDocument", textDocument);
  
  json_object_object_add(o, "position", json_object_new_position(position));
  return o;
};



// (* diagnostics struct parsing *)
// [Trace - 02:39:26 AM] Received notification 'textDocument/publishDiagnostics'.
// Params: {
//   "version": 53,
//   "uri": "file:///home/bollu/software/edtr/build/test/lake-testdir/Main.lean",
//   "diagnostics": [
//     {
//       "source": "Lean 4",
//       "severity": 1,
//       "range": {
//         "start": {
//           "line": 2,
//           "character": 7
//         },
//         "end": {
//           "line": 2,
//           "character": 16
//         }
//       },
//       "message": "unknown identifier 'Stringxxx'",
//       "fullRange": {
//         "start": {
//           "line": 2,
//           "character": 7
//         },
//         "end": {
//           "line": 2,
//           "character": 16
//         }
//       }
//     }
//   ]
// }

