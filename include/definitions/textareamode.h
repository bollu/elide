#pragma once

enum TextAreaMode {
  TAM_Normal,
  TAM_Insert
};

static const char *textAreaModeToString(TextAreaMode mode) {
  switch(mode) {
  case TAM_Normal: return "normal";
  case TAM_Insert: return "insert";
  }
  assert(false && "unreachable: must handle all 'TextAreaMode's.");
}

