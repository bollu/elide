#include "lib.h"
#include "lean_lsp.h"

#ifdef __cplusplus
extern "C"
#endif
const char* __asan_default_options() { return "detect_leaks=0"; }


void test1(AbbreviationDict *dict) {
  const char *unabbrev = "alpha";
  const int unabbrevlen = strlen(unabbrev);

  const char *buf = "beta \\alpha";
  const int buflen = strlen(buf);

  printf("### testing ['%s' ends with abbreviation of '%s']\n", buf, unabbrev);
  AbbrevMatchKind kind = suffix_is_unabbrev(buf, buflen - 1, unabbrev, unabbrevlen);
  printf("  kind: %s\n", abbrev_match_kind_to_str(kind));
  assert(kind == AMK_EXACT_MATCH);
}

void test2(AbbreviationDict *dict) {
  const char *unabbrev = "alpha";
  const int unabbrevlen = strlen(unabbrev);

  const char *buf = "beta \\alp";
  const int buflen = strlen(buf);

  printf("### testing ['%s' ends with abbreviation of '%s']\n", buf, unabbrev);
  AbbrevMatchKind kind = suffix_is_unabbrev(buf, buflen - 1, unabbrev, unabbrevlen);
  printf("  kind: %s\n", abbrev_match_kind_to_str(kind));
  assert(kind == AMK_PREFIX_MATCH);
}

void test3(AbbreviationDict *dict) {
  const char *unabbrev = "alpha";
  const int unabbrevlen = strlen(unabbrev);

  const char *buf = "beta \\alpx";
  const int buflen = strlen(buf);

  printf("### testing ['%s' ends with abbreviation of '%s']\n", buf, unabbrev);
  AbbrevMatchKind kind = suffix_is_unabbrev(buf, buflen - 1, unabbrev, unabbrevlen);
  printf("  kind: %s\n", abbrev_match_kind_to_str(kind));
  assert(kind == AMK_NOMATCH);
}

void test4(AbbreviationDict *dict) {
  const char *unabbrev = "alpha";
  const int unabbrevlen = strlen(unabbrev);

  const char *buf = "beta alpha";
  const int buflen = strlen(buf);

  printf("### testing ['%s' ends with abbreviation of '%s']\n", buf, unabbrev);
  AbbrevMatchKind kind = suffix_is_unabbrev(buf, buflen - 1, unabbrev, unabbrevlen);
  printf("  kind: %s\n", abbrev_match_kind_to_str(kind));
  assert(kind == AMK_NOMATCH);
}


void test5(AbbreviationDict *dict) {
  const char *unabbrev = "alpha";
  const int unabbrevlen = strlen(unabbrev);

  const char *buf = "alpha";
  const int buflen = strlen(buf);

  printf("### testing ['%s' ends with abbreviation of '%s']\n", buf, unabbrev);
  AbbrevMatchKind kind = suffix_is_unabbrev(buf, buflen - 1, unabbrev, unabbrevlen);
  printf("  kind: %s\n", abbrev_match_kind_to_str(kind));
  assert(kind == AMK_NOMATCH);
}

void test6(AbbreviationDict *dict) {
  const char *unabbrev = "alpha";
  const int unabbrevlen = strlen(unabbrev);

  const char *buf = "beta \\alphaa";
  const int buflen = strlen(buf);

  printf("### testing ['%s' ends with abbreviation of '%s']\n", buf, unabbrev);
  AbbrevMatchKind kind = suffix_is_unabbrev(buf, buflen - 1, unabbrev, unabbrevlen);
  printf("  kind: %s\n", abbrev_match_kind_to_str(kind));
  assert(kind == AMK_NOMATCH);
}


void test7(AbbreviationDict *dict) {
  const char *buf = "beta \\al";
  const int buflen = strlen(buf);

  printf("### testing ['%s' matching unabbrevs]\n", buf);
  std::vector<int> matchixs;
  abbrev_dict_get_matching_unabbrev_ixs(dict, buf, buflen - 1, &matchixs);
  for(int ix : matchixs) {
    printf("  %s\n", dict->unabbrevs[ix]);
  }

  // sorted by increasing string length.
  for(int i = 0; i < matchixs.size() - 1; ++i) {
    assert(dict->unabbrevs_len[matchixs[i]] <= dict->unabbrevs_len[matchixs[i+1]]);
  }
}

void test8(AbbreviationDict *dict) {
  const char *buf = "beta \\alpha";
  const int buflen = strlen(buf);

  printf("### testing ['%s' matching unabbrevs]\n", buf);
  std::vector<int> matchixs;
  abbrev_dict_get_matching_unabbrev_ixs(dict, buf, buflen - 1, &matchixs);
  for(int ix : matchixs) {
    printf("  %s\n", dict->unabbrevs[ix]);
  }
  assert(matchixs.size() == 1);
}

void test9(AbbreviationDict *dict) {
  const char *buf = "beta \\alphaxx";
  const int buflen = strlen(buf);

  printf("### testing ['%s' matching unabbrevs]\n", buf);
  std::vector<int> matchixs;
  abbrev_dict_get_matching_unabbrev_ixs(dict, buf, buflen - 1, &matchixs);
  for(int ix : matchixs) {
    printf("  %s\n", dict->unabbrevs[ix]);
  }
  assert(matchixs.size() == 0);
}


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
  assert(dict.is_initialized);

  for(int i = 0; i < dict.nrecords; ++i) {
    if (i < 100) {
      printf("'%s[len=%2d] : '%s'\n",
        dict.unabbrevs[i],
        dict.unabbrevs_len[i],
        dict.abbrevs[i]);
    }
    assert(strlen(dict.unabbrevs[i]) == dict.unabbrevs_len[i]);
  }

  test1(&dict);
  test2(&dict);
  test3(&dict);
  test4(&dict);
  test5(&dict);
  test6(&dict);

  // test abbrev match.
  test7(&dict);
  test8(&dict);
  test9(&dict);

  return 0;

};



