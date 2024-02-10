#pragma once
#include <filesystem>
#include <map>
#include <optional>
#include "subprocess.h"
#include "datastructures/abuf.h"
#include "datastructures/jsonobjectptr.h"
#include "datastructures/lsprequestid.h"
#include "datastructures/lspnonblockingresponse.h"
#include "lean_lsp.h"

namespace fs = std::filesystem;

struct LeanServerCursorInfo {
    fs::path file_path;
    int row;
    int col;
};

enum class LeanServerInitializedKind {
    Uninitialized,
    Initializing,
    Initialized
};

// https://tldp.org/LDP/lpg/node11.html
struct LeanServerState {
    LeanServerInitializedKind initialized = LeanServerInitializedKind::Uninitialized; // whether this lean server has been initalized.
    LspRequestId initialize_request_id;
    // path to the lakefile associated to this lean server, if it uses one.
    std::optional<fs::path> lakefile_dirpath;
    // int parent_buffer_to_child_stdin[2]; // pipe.
    // int child_stdout_to_parent_buffer[2]; // pipe.
    // int child_stderr_to_parent_buffer[2]; // pipe.
    abuf child_stdout_buffer; // buffer to store child stdout data that has not
                              // been slurped yet.
    // abuf child_stderr_buffer; // buffer to store child stderr data that has not
    //                           // been slurped yet.
    // FILE* child_stdin_log_file; // file handle of stdout logging
    // FILE* child_stdout_log_file; // file handle of stdout logging
    // FILE* child_stderr_log_file; // file handle of stderr logging
    // pid_t childpid;
    subprocess_s process;
    int next_request_id = 0; // ID that will be assigned to the next request.
    // number of responses that have been read.
    // invariant: nresponses_read < next_request_id. Otherwise we will deadlock.
    int nresponses_read = 0;

    // server-requests that were recieved when trying to wait for a
    // server-response to a client-request
    std::vector<json_object_ptr> unhandled_server_requests;

    // vector of LSP requests to responses.
    std::map<LspRequestId, json_object_ptr> request2response;

    // low-level API to write strings directly.
    int _write_str_to_child(const char* buf, int len) const;
    // low-level API: read from stdout and write into buffer
    // 'child_stdout_buffer'.
    int _read_stdout_str_from_child_nonblocking();
    // int _read_stderr_str_from_child_blocking();
    // tries to read the next JSON record from the buffer, in a nonblocking fashion.
    // If insufficied data is in the buffer, then return NULL.
    json_object_ptr _read_next_json_record_from_buffer_nonblocking();
    // read the next json record from the buffer, and if the buffer is not full,
    // then read in more data until successful read. Will either hang indefinitely
    // or return a `json_object*`. Will never return NULL.
    // json_object_ptr _read_next_json_record_from_buffer_blocking();

    // high level APIs to write strutured requests and read responses.
    // write a request, and return the request sequence number.
    // this CONSUMES params.
    LspRequestId write_request_to_child_blocking(const char* method, json_object* params);
    // high level APIs to write a notification.
    // this CONSUMES params.
    void write_notification_to_child_blocking(const char* method,
        json_object* params);
    // performs a tick of processing.
    void tick_nonblocking();

    std::optional<json_object_ptr> read_json_response_from_child_nonblocking(LspRequestId request_id);

    // high level APIs
    void get_tactic_mode_goal_state(LeanServerState state,
        LeanServerCursorInfo cinfo);
    void get_term_mode_goal_state(LeanServerState state,
        LeanServerCursorInfo cinfo);
    void get_completion_at_point(LeanServerState state,
        LeanServerCursorInfo cinfo);
    LeanServerState() {};

    void init(std::optional<fs::path> file_path);

private:
};

