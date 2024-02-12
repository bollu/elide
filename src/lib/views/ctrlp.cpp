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
#include "SDL2/SDL_events.h"
#include "imgui/imgui.h"
#include "imgui/imgui_internal.h"

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
    out.push_back("--max-depth"); out.push_back("2");
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

void singleLineTextAreaDraw(SingleLineTextArea* textArea) {

    // Get the font height
    float fontHeight = ImGui::GetFontSize();

    // Optionally add some padding
    float heightPadding = 0.0f;
    float totalHeight =
        fontHeight + heightPadding * 2;  // Total height including padding

    // Calculate the available width
    float width = ImGui::GetContentRegionMaxAbs().x;

    if (ImGui::BeginChild("singleLineTextArea", ImVec2(width, totalHeight),
                          ImGuiChildFlags_None)) {
        _singleLineTextAreaHandleInput(textArea);
        // Draw a rectangle with height equal to font height + padding
        ImVec2 p0 =
            ImGui::GetCursorScreenPos();  // Top-left corner of the rectangle
        ImVec2 p1 =
            ImVec2(p0.x + width,
                   p0.y + totalHeight);  // Bottom-right corner of the rectangle

        ImVec4 bgColor = textArea->mode == TextAreaMode::TAM_Normal
                             ? ImGui::GetStyle().Colors[ImGuiCol_FrameBgHovered]
                             : ImGui::GetStyle().Colors[ImGuiCol_FrameBgActive];

        // Convert ImVec4 (0-1 range) to ImU32
        ImU32 bgColU32 =
            IM_COL32((int)(bgColor.x * 255), (int)(bgColor.y * 255),
                     (int)(bgColor.z * 255), (int)(bgColor.w * 255));
        ImGui::GetWindowDrawList()->AddRectFilled(
            p0, p1, bgColU32);  // Change color as needed

        // Adjust cursor for text, considering padding
        ImGui::SetCursorScreenPos(ImVec2(p0.x, p0.y + heightPadding));

        ImGui::PushItemWidth(
            width);  // Ensure the input text field fits the rectangle
        ImGui::PushStyleColor(
            ImGuiCol_Text, textArea->mode == TextAreaMode::TAM_Normal
                               ? ImGui::GetStyle().Colors[ImGuiCol_TextDisabled]
                               : ImGui::GetStyle().Colors[ImGuiCol_Text]);
        ImGui::TextEx(textArea->text.buf(),
                      textArea->text.buf() + textArea->text.nbytes().size);
        ImGui::PopStyleColor();
        ImGui::PopItemWidth();
        ImGui::EndChild();
    }
};

void _singleLineTextAreaHandleInput(SingleLineTextArea* textArea) {
    ImGuiIO& io = ImGui::GetIO();
    assert(textArea->col <= textArea->text.ncodepoints());
    if (textArea->mode == TAM_Insert) {
        if (io.InputQueueCharacters.Size) {
            for (int n = 0; n < io.InputQueueCharacters.Size; n++) {
                char c = io.InputQueueCharacters[0];
                if (isprint(c)) {
                    textArea->text.insertCodepointBefore(textArea->col, &c);
                    textArea->col += 1;
                }
            }
            io.InputQueueCharacters.resize(0);
        }

        if (ImGui::IsKeyPressed(ImGuiKey::ImGuiKey_Backspace)) {
            if (textArea->col > 0) {
                textArea->text.delCodepointAt(textArea->col.largestIx());
                textArea->col = textArea->col.sub0(1);
            }
        }
    }
    /*
    if (textArea->mode == TAM_Normal) {
        if (e.key.keysym.sym == SDLK_h || e.key.keysym.sym == SDLK_LEFT) {
            textArea->col = textArea->col.sub0(1);
        } else if (e.key.keysym.sym == SDLK_l ||
                   e.key.keysym.sym == SDLK_RIGHT) {
            textArea->col = clampu<Size<Codepoint>>(
                textArea->col + 1, textArea->text.ncodepoints());
        } else if (e.key.keysym.sym == SDLK_DOLLAR ||
                   (e.key.keysym.sym == SDLK_e &&
                    e.key.keysym.mod & KMOD_CTRL)) {
            textArea->col = textArea->text.ncodepoints();
        } else if (e.key.keysym.sym == SDLK_a) {  //  || c == CTRL_KEY('a')) {
            textArea->col = 0;
        } else if (e.key.keysym.sym == SDLK_d ||
                   e.key.keysym.sym == SDLK_d && e.key.keysym.mod & KMOD_CTRL) {
            textArea->text.truncateNCodepoints(textArea->col);
        } else if (e.key.keysym.sym == SDLK_w) {
            // TODO: move word.
            textArea->col = clampu<Size<Codepoint>>(
                textArea->col + 4, textArea->text.ncodepoints());
        } else if (e.key.keysym.sym == SDLK_x) {
            if (textArea->col < textArea->text.ncodepoints()) {
                textArea->text.delCodepointAt(textArea->col.toIx());
            }
        } else if (e.key.keysym.sym == SDLK_b) {
            textArea->col = textArea->col.sub0(4);
            // TODO: move word back.
        } else if (e.key.keysym.sym == SDLK_i) {
            textArea->mode = TAM_Insert;
        } else if (e.key.keysym.sym == SDLK_a) {
            textArea->col = clampu<Size<Codepoint>>(
                textArea->col + 1, textArea->text.ncodepoints());
            textArea->mode = TAM_Insert;
        }
    } else {
        assert(textArea->mode == TAM_Insert);
        if (e.key.keysym.sym == SDLK_c && (e.key.keysym.mod & KMOD_CTRL)) {
            textArea->mode = TAM_Normal;
            // quit and go back to previous state.
        } else if (e.key.keysym.sym ==
                   SDLK_LEFT) {  // || ()c == CTRL_KEY('b')) {
            textArea->col = textArea->col.sub0(1);
        } else if (e.key.keysym.sym ==
                   SDLK_RIGHT) {  // || c == CTRL_KEY('f')) {
            textArea->col = clampu<Size<Codepoint>>(
                textArea->col + 1, textArea->text.ncodepoints());
        } else if (e.key.keysym.sym == SDLK_BACKSPACE) {
            if (textArea->col > 0) {
                textArea->text.delCodepointAt(textArea->col.largestIx());
                textArea->col = textArea->col.sub0(1);
            }
        } else if (!e.key.repeat && e.key.keysym.sym >= SDLK_EXCLAIM &&
                   e.key.keysym.sym <= SDLK_z && e.key.keysym.sym != ' ' &&
                   e.key.keysym.sym != '\t' && e.key.keysym.sym != '\n') {
            const char c = e.key.keysym.sym;
            textArea->text.insertCodepointBefore(textArea->col, &c);
            textArea->col += 1;
        }

        // else if (c == CTRL_KEY('e')) { // readline
        //     textArea->col = textArea->text.ncodepoints();
        // } else if (c == CTRL_KEY('a')) {
        //     textArea->col = 0;
        // } else if (c == CTRL_KEY('k')) {
        //     textArea->text.truncateNCodepoints(textArea->col);
        // } else if (isprint(c) && c != ' ' && c != '\t') {
        //     textArea->text.insertCodepointBefore(textArea->col, (const
        //     char*)&c); textArea->col += 1;
        // }
    }
    */
}

void _ctrlpHandleInput(CtrlPView* view)
{
    // singleLineTextAreaHandleInput(&view->textArea);
    // assert(view->textArea.col <= view->textArea.text.ncodepoints());

    // if (c == 'j' || c == CTRL_KEY('n')) {
    //     view->rgProcess.selectedLine = clamp0u<int>(view->rgProcess.selectedLine + 1, view->rgProcess.lines.size() - 1);
    // } else if (c == 'k' || c == CTRL_KEY('p')) {
    //     view->rgProcess.selectedLine = clamp0(view->rgProcess.selectedLine - 1);
    // };

    // if (view->textArea.mode == TAM_Normal) {
    //     if (c == CTRL_KEY('j') || c == CTRL_KEY('n')) {
    //         view->rgProcess.selectedLine = clamp0u<int>(view->rgProcess.selectedLine + 1, view->rgProcess.lines.size() - 1);
    //     } else if (c == CTRL_KEY('c') || c == 'q') {
    //         // quit and go back to previous state.
    //         view->quitPressed = true;
    //     } else if (c == '\r') {
    //         // pressing <ENTER> either selects if a choice is available, or quits if no choices are available.
    //         if (view->rgProcess.lines.size() > 0) {
    //             view->selectPressed = true;
    //         } else {
    //             view->quitPressed = true;
    //         }
    //     }
    // } else {
    //     assert(view->textArea.mode == TAM_Insert);
    //     if (c == '\r') {
    //         if (view->rgProcess.lines.size() > 0) {
    //             view->selectPressed = true;
    //         } else {
    //             view->quitPressed = true;
    //         }
    //     }
    // }
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

bool ctrlpDraw(CtrlPView* view) {
    if (!g_editor.vim_mode == VimMode::VM_CTRLP) {
        return false;
    }
    // TODO: figure out if window is focused.
    if (ImGui::IsKeyPressed(ImGuiKey_GraveAccent,
                            /*repeat =*/false)) {
        tilde::g_tilde.previousMode = VimMode::VM_CTRLP;
        g_editor.vim_mode = VimMode::VM_TILDE;
    } else if (ImGui::IsKeyPressed(ImGuiKey_Enter)) {
        if (view->rgProcess.lines.size() > 0) {
            view->selectPressed = true;
        } else {
            view->quitPressed = true;
        }
    }
    ImGui::Begin("CtrlP", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("mode (%10s|%10s|%5d matches)",
                textAreaModeToString(view->textArea.mode),
                view->rgProcess.running ? "running" : "completed",
                view->rgProcess.lines.size());
    // ImGui::SetKeyboardFocusHere();
    singleLineTextAreaDraw(&view->textArea);
    ImGui::BeginChild("ctrlp_choices");
    for (int i = 0; i < view->rgProcess.lines.size(); ++i) {
        if (ImGui::Selectable(view->rgProcess.lines[i].to_std_string().c_str(),
                              view->rgProcess.selectedLine == i)) {
            view->rgProcess.selectedLine = i;
        }
    }
    ImGui::EndChild();
    ImGui::End();
    return true;
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

    const char* process_name = "rg";
    // process_name, arg1, ..., argN, NULL
    char** argv = (char**)calloc(sizeof(char*), args.size() + 2);
    argv[0] = strdup(process_name);
    for (int i = 0; i < args.size(); ++i) {
        argv[1 + i] = strdup(args[i].c_str());
    }
    argv[1 + args.size()] = NULL;

    // TODO: add option to chdir()
    const int failure = subprocess_create(working_dir.c_str(), argv, subprocess_options, &this->process);
    assert(!failure);
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
    unsigned nread = subprocess_read_stdout_async(&this->process, buf, BUFSIZE);
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
