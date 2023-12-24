import Lake
open Lake DSL

package «lake-testdir» {
  -- add package configuration options here
}

lean_lib «LakeTestdir» {
  -- add library configuration options here
}

@[default_target]
lean_exe «lake-testdir» {
  root := `Main
}
