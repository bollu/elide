def foo (z : Int) (x : Bool) : Int → Int :=  by exact (fun z => y + 42)

structure Bar where
  a : Int
  b : Int

def f (bar : Bar) : Int := bar.b
