#!/usr/bin/env python3
# Draw overflowing text.
from typing import *

NELLIPSIS_LEFT = 2;
NELLIPSIS_RIGHT = 2;

PADDING_LEFT = 3;
PADDING_RIGHT = 3;

NCOLS = 10;

def draw(x : str , cursor : int) -> str:
  return x



































# ------------------------------------------

def prettify_ASCII_cursor(x : str):
  return x.replace("|", "│");


def golden(x : str, cursor : int):
  out = x[:cursor] + "|" + x[cursor:]
  
NTEST = 0
FAILURES = []
def test(test : str, expected : str):
  cursor = test.find("|");
  assert cursor >= 0;
  x = test[:cursor] + test[cursor+1:]
  out = draw(x, cursor)
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

test("|0123456789", "|01234567..")
test("0|123456789", "0|1234567..")
test("012|3456789", "012|34567..")
test("|0123456789", "|01234567..")
test("|0123456789", "|01234567..")
test("|0123456789", "|01234567..")
test("|0123456789", "|01234567..")

