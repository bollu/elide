#!/usr/bin/env python3


# but it is weird, because it is stateful.
class Point:
  def __repr__(self):
    return f"p({self.i}, {self._box})"

  def floating(self):
    """make a fixed point float"""
    assert self.is_fixed
    return Point(self._i, UNIV)

  def __init__(self, i : int, box : "Box"):
    assert isinstance(i, int)
    assert isinstance(box, AbstractBox)
    self._box = box
    self._i = self._box._clamp(i)

  def copy(self):
    return Point(self._i, self._box.copy())

  @staticmethod
  def mkFixed(i : int):
    """Point with fixed location."""
    p = Point(i, FixedBox(i))
    return p

  # return the fixed value of this box
  @property
  def fixedi(self) -> int:
    assert self.is_fixed
    return self._i

  @property
  def is_fixed(self) -> bool:
    return isinstance(self._box, FixedBox)

  @property
  def is_absurd(self) -> bool:
    return isinstance(self._box, AbsurdBox)

  @property
  def i(self):
    return self._i

  def left(self, len : int):
    return Point(self.i - len, self._box)

  def right(self, len : int):
    return Point(self.i + len, self._box)

  def add_box(self, box):
    print(f"add_box {self} {box}")
    assert isinstance(box, AbstractBox)
    self._box = intersect_box(self._box, box)
    assert isinstance(self._box, AbstractBox)
    self._i = self._box._clamp(self._i)
    return self

  def max(self):
    return self._box.max()
  def min(self):
    return self._box.min()

  # TODO: check that points share box ancestor.
  def __geq__(self, other):
    return self.val >= other.val

  def __leq__(self, other):
    return self.val <= other.val


  def _normalize(self):
    # TODO: check that all boxes have nonempty intersection.
    assert len(self._boxes) > 0
    out = self._boxes[0]
    for box in self._boxes[1:]:
      out = intersect_box(box, out)
    self._boxes = [out]  

def intersect_box(b1 : AbstractBox, b2 : AbstractBox) -> AbstractBox:
  return box(max(b1.max(), b2.max()), min(b1.min(), b2.min()))
      

class Box(AbstractBox):
  """semi open intervals: [l, r)"""
  def __repr__(self):
    return f"B[{self.l}, {self.r})"

  def __init__(self, l, r):
    self.l = l
    self.r = r
    assert isinstance(self.l, Point)
    assert isinstance(self.r, Point)

  def min(self):
    return min(self.l.min(), self.r.min())

  def max(self):
    return max(self.l.max(), self.r.max())

  def copy(self):
    return Box(self.l.copy(), self.r.copy())

  def point(self, i : int):
    p = Point(i, self.copy())
    return p

  def _clamp(self, i : int) -> int:
    if i <= self.l.i: i = self.l.i
    if i >= self.r.i: i = self.r.i
    return i

  # mk child box
  def subbox(self, l, r) -> "Box": 
    import pudb; pudb.set_trace()
    if isinstance(l, int):
      l = Point.mkFixed(l)

    if isinstance(r, int):
      r = Point.mkFixed(r)

    assert isinstance(l, Point)
    assert isinstance(r, Point)
    return intersect_box(self, box(l, r))
    # return box(l.copy().add_box(self.copy()), r.copy().add_box(self.copy()))

  def range(self):
    return range(self.l, self.r)

  def max_length(self, maxlen):
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
  cursor = b.point(i)
  print("cursor: ", cursor)
  textL = cursor.left(4)
  print("  textL: ", textL)
  textR = cursor.right(4)
  print("  textR: ", textR)
  ellipsisL = b.subbox(VIEWSIZE, textL)
  print("  ellipsisL[max_length=infty]: ", ellipsisL)
  ellipsisL = ellipsisL.max_length(3) 
  print("  ellipsisL[max_length=3]: ", ellipsisL)

  ellipsisR = b.subbox(textR.floating(), TEXTNCOL-VIEWSIZE).max_length(3)
  print("  ellipsisL: ", ellipsisL, "ellipsisR: ", ellipsisR)
  out = "  "
  out += "'"
  for i in ellipsisL.range(): out += "."
  out += text[textL.i:textR.i]
  for i in ellipsisR.range(): out += "."
  print(f"  out: {out}")


  
