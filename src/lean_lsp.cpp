#include "lean_lsp.h"
#include "lib.h"
#include "views/tilde.h"
#include <stdlib.h>
#include <string.h>
#include "subprocess.h"


// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#lifeCycleMessages
// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#initialize
json_object* lspCreateInitializeRequest()
{
    // processId
    json_object* o = json_object_new_object();
    json_object_object_add(o, "processId", json_object_new_int(subprocess_getpid()));

    const int CWD_BUF_SIZE = 4096;
    char CWD[CWD_BUF_SIZE];
    if (getcwd(CWD, CWD_BUF_SIZE) == NULL) {
        perror("unable to get cwd.");
    }

    json_object* clientInfo = json_object_new_object();
    json_object_object_add(clientInfo, "name", json_object_new_string("edtr"));
    json_object_object_add(clientInfo, "version", json_object_new_string(VERSION));

    json_object_object_add(o, "clientInfo", clientInfo);

    // json_object_object_add(o, "rootUri", json_object_new_string(CWD));
    json_object_object_add(o, "rootUri", json_object_new_null());
    json_object_object_add(o, "capabilities", lspCreateClientCapabilities());
    return o;
};

void TextDocumentItem::init_from_file_path(fs::path file_path)
{
    FILE* fp = NULL;
    if ((fp = fopen(file_path.string().c_str(), "r")) == NULL) {
        die("unable to create file from path '%s'.", file_path.c_str());
    }

    fseek(fp, 0, SEEK_END);
    int file_len = ftell(fp);
    fseek(fp, 0, SEEK_SET);

    char* text = (char*)calloc(sizeof(char), file_len + 1);
    int nread = fread(text, 1, file_len, fp);
    assert(nread == file_len && "unable to read file");
    fclose(fp);

    this->uri.init_from_file_path(file_path);
    this->languageId = "lean";
    this->version = 0;
    this->text = text;
    free(text);
}

// https://microsoft.github.io/language-server-protocol/specifications/lsp/3.17/specification/#diagnostic
LspDiagnostic json_parse_lsp_diagnostic(json_object* diagnostic, int version)
{
    json_object* rangeo = NULL;
    json_object_object_get_ex(diagnostic, "range", &rangeo);
    assert(rangeo);
    LspRange range = json_object_parse_range(rangeo);

    json_object* messageo = NULL;
    json_object_object_get_ex(diagnostic, "message", &messageo);
    assert(messageo);
    const char* message = json_object_get_string(messageo);

    json_object* severityo = NULL;
    json_object_object_get_ex(diagnostic, "severity", &severityo);
    assert(severityo);
    assert(json_object_get_type(severityo) == json_type_int);
    const int severity = json_object_get_int(severityo);

    tilde::tildeWrite("%s: %s | message: %s | severity: %d", __FUNCTION__,
        json_object_to_json_string(diagnostic),
        message,
        severity);

    assert(severity >= 1);
    assert(severity <= LspDiagnosticSeverity::MAX_DIAGNOSTIC_SEVERITY);

    return LspDiagnostic(range, message,
        static_cast<LspDiagnosticSeverity>(severity),
        version);
}
