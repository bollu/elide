/* An abstraction for the Lean server JSON RPC */


struct LeanServerCursorInfo {
  const char *file_path;
  int row;
  int col;
};

// tactic mode goal.
void lean_server_get_tactic_mode_goal_state(LeanServerCursorInfo cinfo);
// term mode goal
void lean_server_get_term_mode_goal_state(LeanServerCursorInfo cinfo);
// autocomplete.
void lean_server_get_completion_at_point(LeanServerCursorInfo cinfo);

