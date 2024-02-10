#pragma once
#include "algorithms/appendcolwithcursor.h"
#include "algorithms/checkposixcall.h"
#include "algorithms/getfilepathamongstparents.h"
#include "views/ctrlp.h"
#include "views/tilde.h"
#include "datastructures/editorconfig.h"
#include "definitions/keyevent.h"
#include "definitions/ctrlkey.h"
#include "definitions/escapecode.h"

// TODO: get this by ref into the object or something.
extern EditorConfig g_editor; // global editor handle.

/** ctrlp **/

// convert level 'quitPressed' into edge trigger.
bool ctrlpWhenQuit(CtrlPView* view)
{
    const bool out = view->quitPressed;
    view->quitPressed = false;
    return out;
}
bool ctrlpWhenSelected(CtrlPView* view)
{
    const bool out = view->selectPressed;
    view->selectPressed = false;
    return out;
}

FileLocation ctrlpGetSelectedFileLocation(const CtrlPView* view)
{
    assert(view->absolute_cwd.is_absolute());
    assert(view->rgProcess.lines.size() > 0);
    assert(view->rgProcess.selectedLine >= 0);
    assert(view->rgProcess.selectedLine < view->rgProcess.lines.size());
    const abuf& line = view->rgProcess.lines[view->rgProcess.selectedLine];

    tilde::tildeWrite("line: ");
    tilde::tildeWrite(line);

    // search for the first `:`, that is going to be the file path.
    Size<Codepoint> colonOrEndIx = 0;
    for (;
         ((colonOrEndIx < line.ncodepoints()) && (*line.getCodepoint(colonOrEndIx) != ':'));
         ++colonOrEndIx) { }
    // TODO: check that std::string knows what to do when given two pointers.
    const fs::path relative_path_to_file(std::string(line.buf(), line.getCodepoint(colonOrEndIx)));
    const fs::path absolute_path_to_file = view->absolute_cwd / relative_path_to_file;

    tilde::tildeWrite(
        "absolute path '%s' | relative '%s' | found: '%s'",
        view->absolute_cwd.c_str(),
        relative_path_to_file.c_str(),
        absolute_path_to_file.c_str());

    Size<Codepoint> lineNumberOrEndIx = colonOrEndIx;
    for (;
         ((lineNumberOrEndIx < line.ncodepoints()) && (*line.getCodepoint(lineNumberOrEndIx) != ':'));
         ++lineNumberOrEndIx) { }

    int row = 0;
    const std::string lineNumberStr(line.getCodepoint(colonOrEndIx), line.getCodepoint(lineNumberOrEndIx));
    if (lineNumberStr != "") {
        row = atoi(lineNumberStr.c_str()) - 1;
    }
    tilde::tildeWrite("lineNumberStr: %s | row: %d", lineNumberStr.c_str(), row);
    return FileLocation(absolute_path_to_file, Cursor(row, 0));
}

// places glob pattern after the parse.
//   . end of file.
//   . at sigil (@ or #).
const char SIGIL_GLOB_PATTERN_SEPARATOR = ',';
const char SIGIL_FILE_DIRECTORY = '@'; // <email>@<provider.com> (where)
const char SIGIL_CONTENT_PATTERN = '#';

void CtrlPAssertNoWhitespace(const abuf& buf)
{
    for (Ix<Byte> i(0); i < buf.nbytes(); ++i) {
        assert(buf.getByteAt(i) != ' ');
    }
}

std::string CtrlPParseStringText(const abuf& buf, Ix<Byte>& i)
{
    const Ix<Byte> begin = i;
    for (; i < buf.nbytes() && buf.getByteAt(i) != ',' && buf.getByteAt(i) != '#' && buf.getByteAt(i) != '@'; ++i) { }
    // [begin, i)
    return std::string(buf.buf() + begin.ix, i.distance(begin));
}

// sepBy(GLOBPAT,",")("@"FILEPAT|"#"TEXTPAT|","GLOBPAT)*
enum RgPatKind {
    RPK_GLOB,
    RPK_DIR,
    RPK_TEXT,
};
CtrlPView::RgArgs CtrlPView::parseUserCommand(abuf buf)
{
    CtrlPView::RgArgs args;
    RgPatKind k = RgPatKind::RPK_GLOB;
    CtrlPAssertNoWhitespace(buf);
    for (Ix<Byte> i(0); i < buf.nbytes(); i++) {
        // grab fragment.
        std::string fragment = CtrlPParseStringText(buf, i);
        // add fragment.
        if (fragment != "") {
            if (k == RPK_GLOB) {
                args.pathGlobPatterns.push_back(fragment);
            } else if (k == RPK_DIR) {
                args.directoryPaths.push_back(fragment);
            } else if (k == RPK_TEXT) {
                if (args.fileContentPattern == "") {
                    args.fileContentPattern = fragment;
                } else {
                    args.fileContentPattern += "|";
                    args.fileContentPattern += fragment;
                }
            }
        } // if condition: empty fragment.

        // decide next fragment.
        if (buf.nbytes().isEnd(i)) {
            break;
        } else {
            const char sep = buf.getByteAt(i);
            // , separator, be glob pattern.
            if (sep == ',') {
                k = RPK_GLOB;
            } else if (sep == '@') {
                k = RPK_DIR;
            } else if (sep == '#') {
                k = RPK_TEXT;
            } else {
                // someone wrote some malformed ripgrep. Ideally, this should never be allowed to be typed.
            }
        }
    }
    return args;
};

// rg TEXT_STRING PROJECT_PATH -g FILE_PATH_GLOB # find all files w/contents matching pattern.
// rg PROJECT_PATH --files -g FILE_PATH_GLOB # find all files w/ filename matching pattern.
std::vector<std::string> CtrlPView::rgArgsToCommandLineArgs(CtrlPView::RgArgs args)
{
    // search for files
    std::vector<std::string> out;
    if (args.fileContentPattern != "") {
        // we are searching
        out.push_back(args.fileContentPattern);
    } else {
        out.push_back("--files"); // we are searching for files, no pattern.
    }

    // fill in search dirs.
    out.insert(out.end(), args.directoryPaths.begin(), args.directoryPaths.end());

    // fill in file glob pattern for file name.
    for (const std::string& s : args.pathGlobPatterns) {
        out.push_back("-g");
        out.push_back("*" + s + "*");
    }

    out.push_back("-S"); // smart case
    out.push_back("-n"); // line number.
    return out;
};


// compute a "good" path that Ctrlp should start from, based on heuristics. The heuristics are:
// if we find a `lakefile.lean`, that is a good root dir.
// if we find a `.git` folder, that is a good root dir.
// if we find a `.gitignore` folder, that is a good root dir.
// Consider grabbing the working directory with 'fs::absolute(fs::current_path())'
//   if no good starting path is known.
fs::path ctrlpGetGoodRootDirAbsolute(const fs::path absolute_startdir)
{
    assert(absolute_startdir.is_absolute());
    std::optional<fs::path> out;
    if ((out = getFilePathAmongstParents(absolute_startdir, "lakefile.lean"))) {
        return out->parent_path();
    } else if ((out = getFilePathAmongstParents(absolute_startdir, ".git"))) {
        return out->parent_path();
    } else if ((out = getFilePathAmongstParents(absolute_startdir, ".gitignore"))) {
        return out->parent_path();
    } else {
        return absolute_startdir;
    }
}

void ctrlpOpen(CtrlPView* view, VimMode previous_state, fs::path absolute_cwd)
{
    assert(absolute_cwd.is_absolute());
    view->absolute_cwd = absolute_cwd;
    view->textArea.mode = TAM_Insert;
    view->previous_state = previous_state;
    g_editor.vim_mode = VM_CTRLP;
}

void singleLineTextAreaHandleInput(SingleLineTextArea* textArea, int c)
{
    assert(textArea->col <= textArea->text.ncodepoints());
    if (textArea->mode == TAM_Normal) {
        if (c == 'h' || c == KEYEVENT_ARROW_LEFT) {
            textArea->col = textArea->col.sub0(1);
        } else if (c == 'l' || c == KEYEVENT_ARROW_RIGHT) {
            textArea->col = clampu<Size<Codepoint>>(textArea->col + 1, textArea->text.ncodepoints());
        } else if (c == '$' || c == CTRL_KEY('e')) {
            textArea->col = textArea->text.ncodepoints();
        } else if (c == '0' || c == CTRL_KEY('a')) {
            textArea->col = 0;
        } else if (c == 'd' || c == CTRL_KEY('k')) {
            textArea->text.truncateNCodepoints(textArea->col);
        } else if (c == 'w') {
            // TODO: move word.
            textArea->col = clampu<Size<Codepoint>>(textArea->col + 4, textArea->text.ncodepoints());
        } else if (c == 'x') {
            if (textArea->col < textArea->text.ncodepoints()) {
                textArea->text.delCodepointAt(textArea->col.toIx());
            }
        } else if (c == 'b') {
            textArea->col = textArea->col.sub0(4);
            // TODO: move word back.
        } else if (c == 'i') {
            textArea->mode = TAM_Insert;
        } else if (c == 'a') {
            textArea->col = clampu<Size<Codepoint>>(textArea->col + 1, textArea->text.ncodepoints());
            textArea->mode = TAM_Insert;
        }
    } else {
        assert(textArea->mode == TAM_Insert);
        if (c == CTRL_KEY('c')) {
            textArea->mode = TAM_Normal;
            // quit and go back to previous state.
        } else if (c == KEYEVENT_ARROW_LEFT || c == CTRL_KEY('b')) {
            textArea->col = textArea->col.sub0(1);
        } else if (c == KEYEVENT_ARROW_RIGHT || c == CTRL_KEY('f')) {
            textArea->col = clampu<Size<Codepoint>>(textArea->col + 1, textArea->text.ncodepoints());
        } else if (c == KEYEVENT_BACKSPACE) {
            if (textArea->col > 0) {
                textArea->text.delCodepointAt(textArea->col.largestIx());
                textArea->col = textArea->col.sub0(1);
            }
        } else if (c == CTRL_KEY('e')) { // readline
            textArea->col = textArea->text.ncodepoints();
        } else if (c == CTRL_KEY('a')) {
            textArea->col = 0;
        } else if (c == CTRL_KEY('k')) {
            textArea->text.truncateNCodepoints(textArea->col);
        } else if (isprint(c) && c != ' ' && c != '\t') {
            textArea->text.insertCodepointBefore(textArea->col, (const char*)&c);
            textArea->col += 1;
        }
    }
}

void ctrlpHandleInput(CtrlPView* view, int c)
{
    singleLineTextAreaHandleInput(&view->textArea, c);
    assert(view->textArea.col <= view->textArea.text.ncodepoints());

    if (c == 'j' || c == CTRL_KEY('n')) {
        view->rgProcess.selectedLine = clamp0u<int>(view->rgProcess.selectedLine + 1, view->rgProcess.lines.size() - 1);
    } else if (c == 'k' || c == CTRL_KEY('p')) {
        view->rgProcess.selectedLine = clamp0(view->rgProcess.selectedLine - 1);
    };

    if (view->textArea.mode == TAM_Normal) {
        if (c == CTRL_KEY('j') || c == CTRL_KEY('n')) {
            view->rgProcess.selectedLine = clamp0u<int>(view->rgProcess.selectedLine + 1, view->rgProcess.lines.size() - 1);
        } else if (c == CTRL_KEY('c') || c == 'q') {
            // quit and go back to previous state.
            view->quitPressed = true;
        } else if (c == '\r') {
            // pressing <ENTER> either selects if a choice is available, or quits if no choices are available.
            if (view->rgProcess.lines.size() > 0) {
                view->selectPressed = true;
            } else {
                view->quitPressed = true;
            }
        }
    } else {
        assert(view->textArea.mode == TAM_Insert);
        if (c == '\r') {
            if (view->rgProcess.lines.size() > 0) {
                view->selectPressed = true;
            } else {
                view->quitPressed = true;
            }
        }
    }
}

void ctrlpTickPostKeypress(CtrlPView* view)
{
    if (view->textArea.text.whenDirty()) {
        // nuke previous rg process, and clear all data it used to own.
        view->rgProcess.killSync();
        view->rgProcess = RgProcess();
        // invoke the new rg process.
        CtrlPView::RgArgs args = CtrlPView::parseUserCommand(view->textArea.text);
        view->rgProcess.execpAsync(view->absolute_cwd.string(),
            CtrlPView::rgArgsToCommandLineArgs(args));
    }
    // we need a function that is called each time x(.
    view->rgProcess.readLinesNonBlocking();
}

void ctrlpDraw(CtrlPView* view)
{
    abuf ab;
    { // draw text of the search.
        ab.appendstr("\x1b[?25l"); // hide cursor
        ab.appendstr("\x1b[2J"); // J: erase in display.
        ab.appendstr("\x1b[1;1H"); // H: cursor position

        // append format string.
        ab.appendfmtstr(120, "┎ctrlp mode (%10s|%10s|%5d matches)┓\x1b[k\r\n",
            textAreaModeToString(view->textArea.mode),
            view->rgProcess.running ? "running" : "completed",
            view->rgProcess.lines.size());

        const std::string cwd = view->absolute_cwd.string();
        ab.appendfmtstr(120, "searching: '%s'\x1b[k\r\n", cwd.c_str());

        const int NELLIPSIS = 2; // ellipsis width;
        const int VIEWSIZE = 80;
        const int NCODEPOINTS = view->textArea.text.ncodepoints().size;

        // interesting, see that the left and right actually appears to be well-typed.
        // NCODEPOINTS = 40. VIEWSIZE = 10
        // [0, 0] -> [-10, 10] -> [0, 10] -> [0, 10]
        // [1, 1] -> [-9, 11] -> [0, 11] -> [0, 10]?
        const auto intervalText = interval(view->textArea.col.size)
                                      .ldl(VIEWSIZE)
                                      .clamp(0, NCODEPOINTS) // clamp length to be inbounds.
                                      .len_clampl_move_r(VIEWSIZE) // move right hand side point to be inbounds
                                      .clamp(0, NCODEPOINTS); // clamp right to be inbounds

        const auto intervalEllipsisL = interval(0, intervalText.l);
        for (int i = 0; i < NELLIPSIS; ++i) {
            ab.appendstr(ESCAPE_CODE_DULL);
            ab.appendstr(i < intervalEllipsisL.r ? "«" : " ");
            ab.appendstr(ESCAPE_CODE_UNSET);
        }
        ab.appendstr(" ");

        // TODO: why does the right hand size computation not work?
        for (int i = intervalText.l; i <= intervalText.r; ++i) {
            appendColWithCursor(&ab,
                &view->textArea.text,
                i,
                view->textArea.col,
                view->textArea.mode);
        }

        ab.appendstr(" ");

        const auto intervalEllipsisR = interval(intervalText.r, NCODEPOINTS).lregauge();
        for (int i = 0; i < NELLIPSIS; ++i) {
            ab.appendstr(ESCAPE_CODE_DULL);
            ab.appendstr(i < intervalEllipsisR.r ? "»" : " ");
            ab.appendstr(ESCAPE_CODE_UNSET);
        }
        ab.appendstr("\x1b[K\r\n");
    }

    // ab.appendstr(ESCAPE_CODE_UNSET);
    ab.appendstr("\x1b[K\r\n");
    ab.appendstr("━━━━━\x1b[K\r\n");

    { // draw text from rg.
        const int VIEWSIZE = 80;
        const int NELLIPSIS = 3;
        const int NROWS = g_editor.screenrows - 5;

        for (int i = 0; i < view->rgProcess.lines.size() && i < NROWS; ++i) {
            const abuf& line = view->rgProcess.lines[i];
            const int NCODEPOINTS = line.ncodepoints().size;

            if (i == view->rgProcess.selectedLine) {
                ab.appendstr(ESCAPE_CODE_CURSOR_SELECT);
            }

            const interval intervalText = interval(0, VIEWSIZE - NELLIPSIS).clamp(0, NCODEPOINTS);
            for (int i = intervalText.l; i < intervalText.r; ++i) {
                ab.appendCodepoint(line.getCodepoint(Ix<Codepoint>(i)));
            }

            const interval intervalEllipsisR = interval(intervalText.r, NCODEPOINTS).lregauge();
            for (int i = 0; i < 2; ++i) {
                ab.appendstr(ESCAPE_CODE_DULL);
                ab.appendstr(i < intervalEllipsisR.r ? "»" : " ");
                ab.appendstr(ESCAPE_CODE_UNSET);
            }

            if (i == view->rgProcess.selectedLine) {
                ab.appendstr(ESCAPE_CODE_UNSET);
            }

            ab.appendstr("\x1b[K \r\n");
        } // end loop for drawing rg.
    }
#ifndef WIN32
    CHECK_POSIX_CALL_M1(write(STDOUT_FILENO, ab.buf(), ab.len()));
#endif
}

/*
// TODO: figure out how to implement this right!
bool RgProcess::isRunningNonBlocking() {
  if (!this->running) { return false; }
  int status;
  int ret = waitpid(this->childpid, &status, WNOHANG);
  if (ret == 0) {
    // status has not changed.
    return true; // process is still running.
  } else if (ret == -1) {
    perror("waitpid threw error in monitoring `rg` process.");
    die("waitpid threw error in monitoring `rg`!");
  } else {
    assert(ret > 0);
    this->running = false;
  }
  return this->running;
}
*/

// (re)start the process, and run it asynchronously.
void RgProcess::execpAsync(std::string working_dir, std::vector<std::string> args)
{
    this->lines = {};
    this->selectedLine = -1;

    assert(!this->running);
    const int subprocess_options = 
        subprocess_option_inherit_environment |
        subprocess_option_enable_async |
        subprocess_option_no_window | 
        subprocess_option_search_user_path |
        subprocess_option_enable_nonblocking;

    // TODO: add option to chdir()
    // const int failure = 
    //     subprocess_create(argv,
    // assert(false && ("unimplemented " __PRETTY_FUNCTION__ ":" __LINE__)); 
    assert(false && ("unimplemented RgProcess::execpAsync")); 

    // CHECK_POSIX_CALL_0(pipe2(this->child_stdout_to_parent_buffer, O_NONBLOCK));

    // this->childpid = fork();
    // if (childpid == -1) {
    //     perror("ERROR: fork failed.");
    //     exit(1);
    // };

    // if (childpid == 0) {
    //     close(STDIN_FILENO);
    //     close(STDERR_FILENO);
    //     // child->parent, child will only write to this pipe, so close read end.
    //     close(this->child_stdout_to_parent_buffer[PIPE_READ_IX]);
    //     // it is only legal to call `write()` on stdout. So we tie the `PIPE_WRITE_IX` to `STDOUT`
    //     dup2(this->child_stdout_to_parent_buffer[PIPE_WRITE_IX], STDOUT_FILENO);

    //     if (chdir(working_dir) != 0) {
    //         die("ERROR: unable to run `rg, cannot switch to working directory '%s'", working_dir);
    //     };
    //     const char* process_name = "rg";
    //     // process_name, arg1, ..., argN, NULL
    //     char** argv = (char**)calloc(sizeof(char*), args.size() + 2);
    //     argv[0] = strdup(process_name);
    //     for (int i = 0; i < args.size(); ++i) {
    //         argv[1 + i] = strdup(args[i].c_str());
    //     }
    //     argv[1 + args.size()] = NULL;
    //     if (execvp(process_name, argv) == -1) {
    //         perror("failed to launch ripgrep");
    //         abort();
    //     }
    // } else {

    //     // parent<-child, parent will only read from this pipe, so close write end.
    //     close(this->child_stdout_to_parent_buffer[PIPE_WRITE_IX]);

    //     // parent.
    //     assert(this->childpid != 0);
    //     this->running = true;
    // }
};

// kills the process synchronously.
void RgProcess::killSync()
{
    if (!this->running) {
        return;
    }
    subprocess_terminate(&this->process);
    // kill(this->childpid, SIGKILL);
    this->running = false;
}

int RgProcess::_read_stdout_str_from_child_nonblocking()
{
    const int BUFSIZE = 4096;
    char buf[BUFSIZE];
    int nread = subprocess_read_stdout(&this->process, buf, BUFSIZE);
    // TODO: dodgy, do better error handling.
    if (nread == -1) {
        return 0;
    }
    this->child_stdout_buffer.appendbuf(buf, nread);
    return nread;
};

// return true if line was read.
bool RgProcess::readLineNonBlocking()
{
    int newline_ix = 0;
    for (; newline_ix < this->child_stdout_buffer.len(); newline_ix++) {
        if (this->child_stdout_buffer.getByteAt(Ix<Byte>(newline_ix)) == '\n') {
            break;
        }
    }
    if (newline_ix >= this->child_stdout_buffer.len()) {
        return false;
    }
    assert(newline_ix < this->child_stdout_buffer.len());
    assert(child_stdout_buffer.getByteAt(Ix<Byte>(newline_ix)) == '\n');
    // if we find 'a\nbc' (newline_ix=1) we want to:
    //   . take the string 'a' (1).
    //   . drop the string 'a\n' (2).
    this->lines.push_back(this->child_stdout_buffer.takeNBytes(newline_ix));
    if (this->selectedLine == -1) {
        this->selectedLine = 0;
    }
    child_stdout_buffer.dropNBytesMut(newline_ix + 1);
    return true;
}

int RgProcess::readLinesNonBlocking()
{
    _read_stdout_str_from_child_nonblocking();
    int nlines = 0;
    while (this->readLineNonBlocking()) {
        nlines++;
    }
    return nlines;
}
