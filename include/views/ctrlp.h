#pragma once
#include "datastructures/abuf.h"
#include "datastructures/singlelinetextarea.h"
#include "definitions/vimmode.h"
#include <filesystem>
#include <optional>
#include <vector>
#include "subprocess/subprocess.h"
#include "datastructures/filelocation.h"

namespace fs = std::filesystem;

// a struct to encapsulate a child proces that is line buffered,
// which is a closure plus a childpid.
struct RgProcess {
    // whether process has been initialized.
    bool running = false;
    // stdout buffer of the child that is stored here before being processed.
    abuf child_stdout_buffer;
    subprocess_s process;
    // lines streamed from `rg` stdout.
    std::vector<abuf> lines;
    int selectedLine; // currently selected line. Bounds are `[0, lines.size())`

    // start the process, and run it asynchronously.
    void execpAsync(const char* working_dir, std::vector<std::string> args);
    // kills the process synchronously.
    void killSync();

    // attempt to read a single line of input from `rg`.
    // return if line was succesfully read.
    bool readLineNonBlocking();

    // attempt to lines of input from `rg`, and prints the number of lines
    // that were successfully added to `this->lines`.
    int readLinesNonBlocking();

    // TODO: implement this so we can be a little bit smarter,
    // and show an indication that we have finished.
    // // returns whether the process is running.
    // bool isRunningNonBlocking();
private:
    // read from child in a nonblocking fashion.
    int _read_stdout_str_from_child_nonblocking();
};

// data associated to the per-file `CtrlP` view.
struct CtrlPView {
    VimMode previous_state;
    // TODO: extract out the single line text area.
    fs::path absolute_cwd; // default working directory for the search.
    RgProcess rgProcess;
    bool quitPressed = false;
    bool selectPressed = false;
    SingleLineTextArea textArea;

    struct RgArgs {
        std::vector<std::string> pathGlobPatterns; // patterns to filter the search in, added with '-g'.
        std::vector<std::string> directoryPaths; // directories to search in.
        std::string fileContentPattern;

        void debugPrint(abuf* buf) const
        {
            buf->appendfmtstr(100, "pathGlobPatterns [n=%d]\n", (int)pathGlobPatterns.size());
            for (const std::string& s : pathGlobPatterns) {
                buf->appendfmtstr(100, "  . %s\n", s.c_str());
            }
            buf->appendfmtstr(100, "directoryPaths [n=%d]\n", (int)directoryPaths.size());
            for (const std::string& s : directoryPaths) {
                buf->appendfmtstr(100, "  . %s\n", s.c_str());
            }
            buf->appendfmtstr(100, "fileContentPattern '%s'", fileContentPattern.c_str());
        }

        abuf debugPrint() const
        {
            abuf buf;
            this->debugPrint(&buf);
            return buf;
        }
    };
    // #foo
    // *.lean#bar|baz@../@../../ (TODO: pressing TAB should cycle between the components).
    // (GLOBPAT",")*("@"FILEPAT|"#"TEXTPAT|","GLOBPAT)*
    // FILENAMEPAT := <str>
    // FSPAT := "@"<str>
    // TEXTPAT := "#"<str>
    static RgArgs parseUserCommand(abuf buf);

    // rg TEXT_STRING PROJECT_PATH -g FILE_PATH_GLOB # find all files w/contents matching pattern.
    // rg PROJECT_PATH --files -g FILE_PATH_GLOB # find all files w/ filename matching pattern.
    static std::vector<std::string> rgArgsToCommandLineArgs(RgArgs invoke);
};

// convert level 'quitPressed' into edge trigger.
bool ctrlpWhenQuit(CtrlPView* view);
bool ctrlpWhenSelected(CtrlPView* view);
struct FileLocation;
FileLocation ctrlpGetSelectedFileLocation(const CtrlPView* view);
void ctrlpOpen(CtrlPView* view, VimMode previous_state, fs::path cwd);
void ctrlpHandleInput(CtrlPView* view, int c);
void ctrlpTickPostKeypress(CtrlPView* view);
void ctrlpDraw(CtrlPView* view);