#!/usr/bin/env python3
# Litmus tests for cursor behaviour on pressing ENTER for newlines.
from typing import *



class BoundedSemiring:
  @staticmethod
  def combine_bs(self, other, combiner):
    assert isinstance(self, BoundedSemiring)
    assert type(other) is int or type(other) is BoundedSemiring
  
    otherval = other if type(other) is int else other.val
    lo = self.lo if type(other) is int else max(self.lo, other.lo)
    hi = self.hi if type(other) is int else min(self.hi, other.hi)
    return BoundedSemiring(val = combiner(self.val, otherval), hi=hi, lo=lo)

  @staticmethod
  def combine_int(self, other, combiner):
    assert isinstance(self, BoundedSemiring)
    assert type(other) is int or type(other) is BoundedSemiring
  
    otherval = other if type(other) is int else other.val
    return combiner(self.val, otherval)

  def __init__(self, val, lo, hi):
    self.hi = hi
    self.lo = lo
    assert isinstance(self.lo, int)
    assert isinstance(self.hi, int)
    assert self.lo <= self.hi
    self.val = val
    if self.val <= self.lo:
      self.val = self.lo
    if self.val >= self.hi:
      self.val = self.hi
    assert isinstance(self.val, int)
    assert self.lo <= self.val <= self.hi

  def __repr__(self):
    return f"<{self.val}, [{self.lo}, {self.hi}]>"

  def __add__(self, other):
    return BoundedSemiring.combine_bs(self, other, int.__add__)

  def __sub__(self, other):
    return BoundedSemiring.combine_bs(self, other, int.__sub__)

  def __iadd__(self, other):
    new = self + other
    self.val = new.val
    self.hi = new.hi
    self.lo = new.lo
    return self

  def __lt__(self, other):
    return BoundedSemiring.combine_int(self, other, int.__lt__)

  def __gt__(self, other):
    return BoundedSemiring.combine_int(self, other, int.__gt__)

  def __le__(self, other):
    return BoundedSemiring.combine_int(self, other, int.__le__)

  def __ge__(self, other):
    return BoundedSemiring.combine_int(self, other, int.__ge__)

  def is_hi(self): return self.val == self.hi

# Space is represented by `~` for easy reading.
# cursor is in [0, len(x)]. Represents the index of the character it is behind.
# return the (cursor_row, cursor_col, row1, row2).
def enter_space(x : str, cursor: int) -> Tuple[int, int, str, str]:
  assert 0 <= cursor <= len(x)
  cursor = BoundedSemiring(cursor, lo=0, hi=len(x))
  k = BoundedSemiring(0, lo=0, hi=cursor.val)
  print(k)

  nspaces = BoundedSemiring(0, lo=0, hi=len(x))
  while not nspaces.is_hi() and x[nspaces.val] == '~':
    k += 1
    nspaces += 1
  # k = min(nspaces, cursor) # spaces before cursor.
  # l = max(0, nspaces - cursor) # spaces after cursor.
  l = nspaces - cursor
  row = 1
  col = (k - l)
  print(f"x: {x} | cursor: {cursor} | k: {k} | l: {l} | col: {col}")
  col = col.val
  str1 = x[0:cursor.val]
  str2 = '~' * col + x[cursor.val:]
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
