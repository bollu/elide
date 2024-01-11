{ }:

let
  pkgs = import <nixpkgs> { };
in
  pkgs.stdenv.mkDerivation {
    name = "elide-1.0.0";
    src = ./.;
    buildInputs = [ pkgs.cmake pkgs.json_c pkgs.libuv
    pkgs.doxygen pkgs.graphviz pkgs.ninja pkgs.z3 pkgs.neovim pkgs.clang];
  }

