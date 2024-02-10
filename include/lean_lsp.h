// APIs to create objects for the lean LSP
#pragma once
#include "uri_encode.h"
#include <assert.h>
#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <filesystem>
#include <iostream>
#include <iterator>
#include <json-c/json.h>
#include <optional>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/ttydefaults.h>
#include <termios.h>
#include <unistd.h>
#include <unordered_map>
#include <vector>

namespace fs = std::filesystem;

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

static json_object* lspCreateClientCapabilities()
{
    json_object* o = json_object_new_object();

    json_object* text_document_capabilities = json_object_new_object();

    json_object_object_add(o, "textDocument", text_document_capabilities);
    return o;
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#lifeCycleMessages
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initialize
json_object* lspCreateInitializeRequest();

struct Uri {
    std::string uri;

    operator bool() const
    {
        return uri.size() > 0;
    }

    bool is_initialized() const
    {
        return bool(*this);
    }

    Uri() { this->uri = ""; }
    Uri(fs::path file_path) { this->init_from_file_path(file_path); }

    void init_from_file_path(fs::path file_path)
    {
        const std::string file_segment_uri = "file://";
        const std::string file_uri_unencoded = file_segment_uri + std::string(file_path);

        std::string out;
        out.resize(file_uri_unencoded.size() * 4 + 1);
        uri_encode(file_uri_unencoded.c_str(), file_uri_unencoded.size(),
            &out[0]);
        this->uri = out;
    }

    static fs::path parse(const char* uri_str)
    {
        std::string decoded;
        decoded.resize(strlen(uri_str) + 1);
        uri_decode(uri_str, strlen(uri_str), &decoded[0]);

        const char* FILE = "file://";
        assert(decoded.size() > strlen(FILE));
        for (int i = 0; i < strlen(FILE); ++i) {
            assert(decoded[i] == FILE[i]);
        };
        return fs::path(decoded.substr(strlen(FILE), std::string::npos));
    }
};

static json_object* json_object_new_uri(Uri uri)
{
    return json_object_new_string(uri.uri.c_str());
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocumentItem
struct TextDocumentItem {
    Uri uri;
    std::string languageId;
    int version = -1; // version of the document. (it will increase after each change, including undo/redo).
    std::string text;

    // TextDocumentItem() = default;

    TextDocumentItem(Uri uri, std::string languageId, int version, std::string text)
        : uri(uri)
        , languageId(languageId)
        , version(version)
        , text(text) {};

    TextDocumentItem& operator=(const TextDocumentItem& other)
    {
        uri = other.uri;
        languageId = other.languageId;
        version = other.version;
        text = other.text;
        return *this;
    }

    TextDocumentItem(const TextDocumentItem& other)
    {
        *this = other;
    }

    void init_from_file_path(fs::path file_path);
};

static json_object* json_object_new_text_document_item(TextDocumentItem item)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "uri", json_object_new_uri(item.uri));
    json_object_object_add(o, "languageId", json_object_new_string(item.languageId.c_str()));
    json_object_object_add(o, "version", json_object_new_int(item.version));
    json_object_object_add(o, "text", json_object_new_string(item.text.c_str()));
    return o;
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didOpen
static json_object* lspCreateDidOpenTextDocumentNotifiation(TextDocumentItem item)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "textDocument", json_object_new_text_document_item(item));
    return o;
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#textDocument_didChange
static json_object* lspCreateDidChangeTextDocumentRequest(TextDocumentItem item)
{
    json_object* o = json_object_new_object();
    // VersionedTextDocumentIdentifier
    json_object* textDocument = json_object_new_object();
    json_object_object_add(textDocument, "uri", json_object_new_uri(item.uri));
    json_object_object_add(textDocument, "version", json_object_new_int(item.version));

    json_object_object_add(o, "textDocument", textDocument);

    json_object* contentChanges = json_object_new_array();

    json_object* contentChangeEvent = json_object_new_object();
    json_object_object_add(contentChangeEvent, "text", json_object_new_string(item.text.c_str()));

    json_object_array_add(contentChanges, contentChangeEvent);
    json_object_object_add(o, "contentChanges", contentChanges);
    return o;
}

// TODO:
static json_object* lspCreateInitializedNotification()
{
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

// TODO: rename to JsonLspPosition.
struct LspPosition {
    int row = -42; // zero indexed line number.
    int col = -42; // utf-8 offset for column.

    LspPosition(int row, int col)
        : row(row)
        , col(col) {};
};

struct LspRange {
    LspPosition start;
    LspPosition end;
    LspRange(LspPosition start, LspPosition end)
        : start(start)
        , end(end)
    {
    }
};

static json_object* json_object_new_position(LspPosition position)
{
    json_object* o = json_object_new_object();
    json_object_object_add(o, "line", json_object_new_int64(position.row));
    json_object_object_add(o, "character", json_object_new_int64(position.col));
    return o;
}

static LspPosition json_object_parse_position(json_object* o)
{
    assert(o != nullptr);
    json_object* line = nullptr;
    json_object_object_get_ex(o, "line", &line);
    assert(line != nullptr);
    const int linei = json_object_get_int(line);

    json_object* character = nullptr;
    json_object_object_get_ex(o, "character", &character);
    assert(character != nullptr);
    const int endi = json_object_get_int(character);
    return LspPosition(linei, endi);
}

static LspRange json_object_parse_range(json_object* o)
{
    assert(o != nullptr);
    json_object* start = nullptr;
    json_object_object_get_ex(o, "start", &start);
    assert(start != nullptr);
    const LspPosition startPosition = json_object_parse_position(start);

    json_object* end = nullptr;
    json_object_object_get_ex(o, "end", &end);
    assert(end != nullptr);
    const LspPosition endPosition = json_object_parse_position(end);
    return LspRange(startPosition, endPosition);
}

// $/lean/plainTermGoal
static json_object* lspCreateLeanPlainTermGoalRequest(Uri uri, const LspPosition position)
{
    assert(uri);
    json_object* o = json_object_new_object();
    // textDocumentIdentifier
    json_object* textDocument = json_object_new_object();
    json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
    json_object_object_add(o, "textDocument", textDocument);

    json_object_object_add(o, "position", json_object_new_position(position));
    return o;
};

// $/lean/plainGoal
static json_object* lspCreateLeanPlainGoalRequest(Uri uri, const LspPosition position)
{
    assert(uri);
    json_object* o = json_object_new_object();
    // textDocumentIdentifier
    json_object* textDocument = json_object_new_object();
    json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
    json_object_object_add(o, "textDocument", textDocument);

    json_object_object_add(o, "position", json_object_new_position(position));
    return o;
};

// textDocument/hover
static json_object* lspCreateTextDocumentHoverRequest(Uri uri, const LspPosition position)
{
    assert(uri);
    json_object* o = json_object_new_object();
    // textDocumentIdentifier
    json_object* textDocument = json_object_new_object();
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
static json_object* lspCreateTextDocumentCompletionRequest(Uri uri, const LspPosition position, CompletionTriggerKind triggerKind)
{
    assert(uri);
    json_object* o = json_object_new_object();
    // textDocumentIdentifier
    json_object* textDocument = json_object_new_object();
    json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
    json_object_object_add(o, "textDocument", textDocument);

    json_object_object_add(o, "position", json_object_new_position(position));
    json_object* context = json_object_new_object();
    json_object_object_add(context, "triggerKind", json_object_new_int(int(triggerKind)));
    json_object_object_add(o, "context", context);
    return o;
};

// textDocument/definition
static json_object* lspCreateTextDocumentDefinitionRequest(Uri uri, const LspPosition position)
{
    assert(uri);
    json_object* o = json_object_new_object();
    // textDocumentIdentifier
    json_object* textDocument = json_object_new_object();
    json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
    json_object_object_add(o, "textDocument", textDocument);

    json_object_object_add(o, "position", json_object_new_position(position));
    return o;
};

// textDocument/declaration
static json_object* lspCreateTextDocumentDeclarationRequest(Uri uri, const LspPosition position)
{
    assert(uri);
    json_object* o = json_object_new_object();
    // textDocumentIdentifier
    json_object* textDocument = json_object_new_object();
    json_object_object_add(textDocument, "uri", json_object_new_uri(uri));
    json_object_object_add(o, "textDocument", textDocument);

    json_object_object_add(o, "position", json_object_new_position(position));
    return o;
};

enum LspDiagnosticSeverity {
    Error = 1,
    Warning = 2,
    Information = 3,
    Hint = 4,
    MAX_DIAGNOSTIC_SEVERITY = 5
};

struct LspDiagnostic {
    LspRange range;
    std::string message;
    LspDiagnosticSeverity severity;
    int version;

    LspDiagnostic(LspRange range, std::string message, LspDiagnosticSeverity severity, int version)
        : range(range)
        , message(message)
        , severity(severity)
        , version(version)
    {
        assert(severity <= MAX_DIAGNOSTIC_SEVERITY);
        assert(severity >= 1);
    };
};

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnostic
LspDiagnostic json_parse_lsp_diagnostic(json_object* diagnostic, int version);


