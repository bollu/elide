# elIDE: Elegant Metamodal Lean4 IDE

A [Metamodal](https://en.wikipedia.org/wiki/Metamodernism) IDE for Lean.
As I broadly see it, there are two kinds of proof assistant editors:
1. emacs-like (e.g. [Proof General](https://proofgeneral.github.io/)) which exposes
  an emacs-style interface with many options and keybinds to make things happen.
2. LSP-like (e.g. VSCode, [lean.nvim](https://github.com/Julian/lean.nvim)), which
   use the always-on LSP to provide instant, zero interaction feedback.

Both of these leave me just shy of perfectly content. The emacs experience I find
janky, since it intermingles text editing with proof state management concerns.
The LSP experience I find unperformant, since the always-on LSP leads to my laptop
doubling as a great space heater.

The natural conclusion is that a real **modal** proof assistant IDE has not been written.
This is my Christmas 2023 effort to fill this unfilled niche in the market.

#### Features

- [x] Modal infoview, hover, and lean message list.
- [x] built-in Ripgrep for fast, fuzzy file and pattern search.
- [x] Vim keybindings.

#### Brutalist Anti-Features

- No syntax highlighting.
- Sits in at ~4000 LoC of C/C++.
- No multi-file editing support. To edit another file, just open it in another instance of `elide`.

#### Building

```cpp
$ mkdir build && cd build && cmake ../ && make -j4
```

#### References

- [KILO: build your own text editor](https://viewsourcecode.org/snaptoken/kilo/).
- [vis editor](https://github.com/martanne/vis).
- [Language server protocol](https://microsoft.github.io/language-server-protocol/) reference.
- [Atom's new buffer implementation](https://web.archive.org/web/20221129082104/http://blog.atom.io/2017/10/12/atoms-new-buffer-implementation.html)
- [Notcurses](https://notcurses.com/)


#### Developer Diary

##### Representing Cursor Positions

Think of the cursor as the index *before* which text is inserted.
In this model of the word, the index `0` means that text is inserted before the `0`th character.
If we now have a line `ab`, the following indexes make sense, when we want to insert a `x`:

- `|ab` → `x|ab`: insert before `0`.
- `a|b` → `a|xb`: insert before `1`.
- `ab|` → `abx|`: insert before `2`.

Thus, valid cursor posittions are in the interval `[0, 2]`.
This makes all of the code annoying,
because we are forever haunted by the thread of an error at the boundary.

The string algorithmics folks always append a `$` to a string.
Unfortunately, we have nothing so nice.
We can consider using the NULL character,
but this to me felt like it created far more problems than it solved, and I thus abandoned it.

##### Bounded Semiring

Most calculations inside an editor seem to require constant clamping,
either in the interval `[0, size)` or `[0, size]`.
Abstracting this into a class called `bounded_ring` which enforces these bounds might be a good idea.
A poor man's approximation is to create functions called `clamp`, `clamp0`, `clamp0u` and variations which
clamp according to various rules.

An example of such a nice development is in [test/litmus/enter.py](https://github.com/bollu/elide/blob/fb76abc0ed2258d3c57453b1ce0067b9b690ea6a/test/litmus/enter.py#L75-L87).
It shows the difference between have to think of `min/max`ing repeatedly,
versus simply substracting data and knowing that it will be in the right bounds.

##### Level Trigger to Edge Trigger

I learnt this trick from [Dear ImGUI](https://github.com/ocornut/imgui), and is useful to convert persistent information
(e.g. has the text area changed) into a trigger (e.g. is this the *first* time I have noticed that the text area has changed, so I can send this to the LSP and mark the text area as clean again.). 


For any persistent information, such as `bool TextArea::isDirty`, which tells us whether the text area is dirty,
we create a method `bool TextArea::whenDirty()`.
This method returns the *current* state of `isDirty`, and resets `isDirty` to false. In code:

```cpp
bool TextArea::whenDirty() { bool b = isDirty; isDirty = false; return b; };
```

Then, when performing event handling, one writes code as:

```cpp
if (textArea.whenDirty()) { /* send request to LSP */ }
```

This pattern helps avoid one from forgetting flushing the `dirty` flag.
Furthermore, it cleanly encapsulates react-on-state-change behaviours.
idea 


##### Undo via CRTP

A classical way to implement undo/redo is via the [memento pattern](https://en.wikipedia.org/wiki/Memento_pattern).
Most descriptions require the parent class (e.g. `File`) that owns the state to have a `FileState File::state` field.
This `state` is then pushed onto an undo/redo stack. 
This is quite annoying, since it requires a level of indirection for every member access of `File`,
or worse, 
a proliferation of `get()/set()`.
A clean solution via [CRTP](https://en.cppreference.com/w/cpp/language/crtp) presented itself.
A class `Undoer<S>` holds undo state `S`. Have `Undoer<S> : public S` via CRTP.
Then, every member of `S` is transparently accessible via `Undoer<S>`.
Concretely, one does `class File : public Undoer<FileState>`.
Thus, every member of `FileState` becomes an implicit member of `File`.
Voila, clean memento pattern.


##### Undo/Redo with Memento

How precisely does one implement `undo/redo` via memento?
I had previouly used the [Command pattern](https://en.wikipedia.org/wiki/Command_pattern) and the classical `undo/redo` stack.
However, this does not *directly* adapt, a little thought is required.

We begin by defining an *undo sequence* to be a sequence of `undo/redo` moves a user is performing.
The user is said to be *in a undo sequence* if at least one undo has been pressed, and only undos and redos have been pressed since.

When a user *begins* an undo sequence, it is important to get a checkpoint of the current state and push it into the redo stack.
But this must only be done one, at the beginning of the undo sequence!




