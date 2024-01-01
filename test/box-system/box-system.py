#!/usr/bin/env python3


class BoxIdeal:
 def __init__(self, boxes):
   self.boxes = boxes

class Box:
  """semi open intervals: [l, r)"""
  def __repr__(self):
    return f"B[{self.l}, {self.r})"

  def __init__(self, l, r, parent=None):
    self.l = l
    self.r = r
    assert isinstance(self.l, int)
    assert isinstance(self.r, int)

    if not parent:
      self.parent = self


  # mk child box
  # pl---l--r--pr
  def subbox(self, l:Box, r : Box) -> "Box": 
    return Box(l, r, self)

  def subpoint(self, i : int) -> "Box":
    return self.subbox(i, i+1)

  def left(self, i:int):
    return self.subbox(self.l-i, self.r-i)

  def right(self, i:int):
    return self.subbox(self.l+i, self.r+i)

  def clamp(self, i:int):
    if i <= self.l: i = self.l
    if i >= self.r: i = self.r
    if parent != self.parent:
      i = self.parent.clamp(i)
    return i
    
  def range(self):
    l = self.clamp(self.l)
    r = self.clamp(self.r)
    return range(l, r)

  def maxlen(self, maxlen):
    # capital = fixed, small = loose
    if self.l.is_fixed:
      # [L, ?)
      if self.r.is_fixed:
        # [L, R)
        assert self.r.val - self.l.val <= maxlen
      else:
        # [L, L+maxlen)
        newbox = box(self.l.fixedi, self.l.fixedi+maxlen)
        return box(self.l, self.r.copy().add_box(newbox))
      
    else:
      # [l, ?)
      if self.r.is_fixed:
        # [l, R)
        newbox = box(self.r.fixedi-maxlen, self.r.fixedi)
        return box(self.l.copy().add_box(newbox), self.r)
      else:
        raise RuntimeError(f"ERR: '{self}, can't impose box size with box points floating! ")

def box(l, r):
  if isinstance(l, int):
    l = Point.mkFixed(l)
  else:
    assert isinstance(l, Point)

  if isinstance(r, int):
    r = Point.mkFixed(r)
  else:
    assert isinstance(r, Point)
    
  if l.is_absurd or r.is_absurd:
    return AbsurdBox()

  if l.is_fixed and r.is_fixed:
    if l.fixedi > r.fixedi:
      return AbsurdBox()
  return Box(l, r)

TEXTNCOL = 30
VIEWSIZE = 10

text = "0there@is@a@time@when@the@operation@of@the@machine@becomes@so@odious@so@sick@at@heart$"
text = text[:TEXTNCOL]

for i in range(0, TEXTNCOL):
  b = box(0, TEXTNCOL)
  # presheaf? inclusion maps given by actual inclusion.
  print("b: ", b)
  cursor = b.subpoint(i) # cursor = 20. Fixed point after normalization.
  print("cursor: ", cursor)
  textL = cursor.left(4)
  print("  textL: ", textL)
  textR = cursor.right(4)
  print("  textR: ", textR)
  ellipsisL = b.subbox(VIEWSIZE, textL)
  print("  ellipsisL[maxlen=infty]: ", ellipsisL)
  ellipsisL = ellipsisL.maxlen(3) 
  print("  ellipsisL[maxlen=3]: ", ellipsisL)

  ellipsisR = b.subbox(textR, TEXTNCOL-VIEWSIZE).maxlen(3)
  print("  ellipsisL: ", ellipsisL, "ellipsisR: ", ellipsisR)
  out = "  "
  out += "'"
  for i in ellipsisL.range(): out += "."
  out += text[textL.i:textR.i]
  for i in ellipsisR.range(): out += "."
  print(f"  out: {out}")


  
