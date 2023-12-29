# elIDE: Elegant Lean IDE

Metamodal IDE for Lean.
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
cmake -DCMAKE_BUILD_TYPE=Debug -DCMAKE_EXPORT_COMPILE_COMMANDS=ON -GNinja ../
```

#### References

- [KILO: build your own text editor](https://viewsourcecode.org/snaptoken/kilo/).
- [vis editor](https://github.com/martanne/vis).
- [Language server protocol](https://microsoft.github.io/language-server-protocol/) reference.

