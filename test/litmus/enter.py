#!/usr/bin/env python3
# Litmus tests for cursor behaviour on pressing ENTER for newlines.
from typing import *

# Space is represented by `~` for easy reading.
# cursor is in [0, len(x)]. Represents the index of the character it is behind.
# return the (cursor_row, cursor_col, row1, row2).
def enter_space(x : str, cursor: int) -> Tuple[int, int, str, str]:
  assert 0 <= cursor <= len(x)

  k = 0 # number of initial spaces.
  while k < cursor and x[k] == '~':
    k += 1

  l = 0 # number of spaces afterwards
  if k <= cursor:
    while x[k + l] == '~':
      l += 1
  
  print(f"x: {x} | cursor: {cursor} | k: {k} | l: {l}")
  row = 1
  col = max(k - l, 0)
  str1 = x[0:cursor]
  str2 = '~' * col + x[cursor:]
  return (row, col, str1, str2)

def prettify_ASCII_cursor(x : str):
  return x.replace("|", "│");

NTEST = 0
FAILURES = []


def run_test(test : str, expected: Optional[str]=None):
  cursor = test.find("|");
  assert cursor >= 0;
  x = test[:cursor] + test[cursor+1:]
  (row, col, str1, str2) = enter_space(x, cursor)
  strs = [str1, str2]
  strs[row] = strs[row][:col] + "|" + strs[row][col:]
  out = f"{strs[0]}\n{strs[1]}"
  global NTEST
  NTEST += 1
  print(f"test {NTEST}:")
  print("─────[before]─────")
  print(prettify_ASCII_cursor(test))
  print("─────[after]──────")
  print(prettify_ASCII_cursor(out))
  status = "[none]"
  did_fail = expected and out != expected
  if did_fail:
    status = "[:(]"
    global FAILURES
    FAILURES.append(NTEST)
  else:
    status = "[:)]"
  print("─" * 4 + status + "─"*4)
  if did_fail:
    assert expected
    print("─────[expected]──────")
    print(prettify_ASCII_cursor(expected))

  print("\n")


run_test("~~~|~int x = 0", "~~~\n~~|~int x = 0");
run_test("~|~~~int x = 0", "~\n|~~~int x = 0");
run_test("~~int x|= 0", "~~int x\n~~|= 0");
run_test("~~int x = 0|", "~~int x = 0\n~~|");
run_test("|~~int x = 0", "\n|~~int x = 0");
run_test("~~int~|x~=~0", "~~int~\n~~|x~=~0");
run_test("int~|~x~=~0", "int~\n|~x~=~0");
if FAILURES:
  print(f"Final failures: {FAILURES}")
else:
  print("Final: all succeeded!")
