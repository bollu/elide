def foo (z : Int) (x : Bool) : Int → Int :=  by exact (fun z => y + 42)

structure Bar where
  a : Int
  b : Int


#eval 10


def f (bar : Bar) : Int := 
  by 
  . cases bar  
    case mk a b =>
      exact b

#eval f  