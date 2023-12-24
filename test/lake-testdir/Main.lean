import «LakeTestdir»

def foo (y : String) : String := by
  have H := hello
  exact (H ++ y)

def main : IO Unit :=
  IO.println s!"Hello, {hello}!"
