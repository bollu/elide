#pragma once
#include <filesystem>
#include <vector>

namespace fs = std::filesystem;

// TODO: move this into Abbreviation
// returns true if str ends in \<prefix of potential_abbrev>
enum AbbrevMatchKind {
    AMK_NOMATCH = 0,
    AMK_PREFIX_MATCH = 1,
    AMK_EXACT_MATCH = 2, // exact match
    AMK_EMPTY_STRING_MATCH = 3, // match against the empty string.
};


// unabbrevs[i] ASCII string maps to abbrevs[i] UTF-8 string.
struct AbbreviationDict {
    char** unabbrevs = nullptr;
    char** abbrevs = nullptr;
    int* unabbrevs_len = nullptr; // string lengths of the unabbrevs;
    int nrecords = 0;
    bool is_initialized = false;
};

// return the index of the all matches, for whatever match exists. Sorted to be matches
// where the match string has the smallest length to the largest length.
// This ensures that the AMK_EXACT_MATCHes will occur at the head of the list.
void abbrev_dict_get_matching_unabbrev_ixs(AbbreviationDict* dict,
    const char* buf, int finalix, std::vector<int>* matchixs);

struct SuffixUnabbrevInfo {
    AbbrevMatchKind kind = AbbrevMatchKind::AMK_NOMATCH;
    int matchlen = -1; // length of the math if kind is not NOMATCH;
    int matchix = -1; // index of the match if kind is not NOMATCH

    // return the information of having no match.
    static SuffixUnabbrevInfo nomatch()
    {
        return SuffixUnabbrevInfo();
    }
};

// return the best unabbreviation for the suffix of `buf`.
SuffixUnabbrevInfo abbrev_dict_get_unabbrev(AbbreviationDict* dict, const char* buf, int finalix);


// get length <len> such that
//    buf[:finalix) = "boo \alpha"
//    buf[:finalix - <len>] = "boo" and buf[finalix - len:finalix) = "\alpha".
// this is such that buf[finalix - <len>] = '\' if '\' exists,
//     and otherwise returns <len> = 0.
int suffix_get_unabbrev_len(const char* buf, int finalix, const char* unabbrev, int unabbrevlen);
// return whether there is a suffix of `buf` that looks like `\<unabbrev_prefix>`.
AbbrevMatchKind suffix_is_unabbrev(const char* buf, int finalix, const char* unabbrev, int unabbrevlen);
const char* abbrev_match_kind_to_str(AbbrevMatchKind);


struct json_object;

// Load the abbreviation dictionary from the filesystem.
// NOTE: this steals the pointer to `o`.
void load_abbreviation_dict_from_json(AbbreviationDict* dict, json_object* o);

// Load the abbreviation dictionary from the filesystem.
void load_abbreviation_dict_from_file(AbbreviationDict* dict, fs::path abbrev_path);


