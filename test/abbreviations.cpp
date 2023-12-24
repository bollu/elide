#include "lib.h"
#include "lean_lsp.h"

#ifdef __cplusplus
extern "C"
#endif
const char* __asan_default_options() { return "detect_leaks=0"; }

int main() {
  printf("### testing [exe path]\n");
  printf("  %s\n", get_executable_path());

  const char *abbrev_path = get_abbreviations_dict_path();
  printf("testing [abbreviations.json path]\n");
  printf("  %s\n", abbrev_path);

  AbbreviationDict dict;
  printf("testing [loading abbreviations.json path]\n");
  load_abbreviation_dict_from_file(&dict, abbrev_path);
  printf("  nrecords: %d\n", dict.nrecords);
  assert(dict.initialized);

  for(int i = 0; i < dict.nrecords; ++i) {
    printf("%20s[len=%2d] : %4s\n", dict.unabbrevs[i], dict.unabbrevs_len[i], dict.abbrevs[i]);
    assert(strlen(dict.unabbrevs[i]) == dict.unabbrevs_len[i]);
  }
  return 0;

};



