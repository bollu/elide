#pragma once
#include <assert.h>

// return the length (in _buf) of the next code point.
static int utf8_next_code_point_len(const char *str) {
  assert(str);
  char c = str[0];
  const bool b7 = c & (1 << 7);
  const bool b6 = c & (1 << 6);
  const bool b5 = c & (1 << 5);
  const bool b4 = c & (1 << 4);
  const bool b3 = c & (1 << 3);
  if (b7 == 0) {
    return 1;
  } else if (b7 == 1 && b6 == 1 && b5 == 0) {
    return 2;
  } else if (b7 == 1 && b6 == 1 && b5 == 1 &&  b4 == 0) {
    return 3;
  } else if (b7 == 1 && b6 == 1 && b5 == 1 &&  b4 == 1 && b3 == 0) {
    return 4;
  } else {
    assert(false && "unknown UTF-8 width");
  }
  assert(false && "unknown UTF-8 width");
  return 0;
};

// return the pointer to the next code point.
// Precondition: assumes that `str` is already code_point aligned.
static const char *utf8_next_code_point(const char *str) {
  return str + utf8_next_code_point_len(str); 
}

// return the length (in _buf) of the previous code point at index `ix`.
// If incomplete, then returns `0`. 
// byte1    | byte2    |  byte3   | byte4    |
// 0xxxxxxx |          |          |          | 
// 110xxxxx | 10xxxxxx |          |          |
// 1110xxxx | 10xxxxxx | 10xxxxxx |
// 11110xxx | 10xxxxxx | 10xxxxxx | 10xxxxxx
static int utf8_prev_code_point_len(const char *str, int ix) {
  assert (ix >= 0);
  int c0 = str[ix];
  const bool b0_7 = c0 & (1 << 7);
  const bool b0_6 = c0 & (1 << 6);
  if (b0_7 == 0) {
    // 0xxxxxxx
    // ^~~~~ix
    return 1; 
  }
  assert(b0_7);
  assert(b0_6 == 0);
  // 10xxxxxx
  // ^~~~~ix

  // ### it is at least 2 characters long.
  assert (ix >= 1);
  unsigned char c1 = str[ix-1];
  const bool b1_7 = c1 & (1 << 7);
  const bool b1_6 = c1 & (1 << 6);
  const bool b1_5 = c1 & (1 << 5);
  assert(b1_7);  
  // 1xxxxxxxx; 10xxxxxx
  //            ^~~~~ix
  if (b1_6) {
    // 11xxxxxx ; 10xxxxxx
    //            ^~~~~ix
    assert(b1_5 == 0);
    // 110xxxxx ; 10xxxxxx
    //            ^~~~~ix
    return 2;
  }
  assert(b1_6 == 0);
  // 10xxxxxxx; 10xxxxxx
  //            ^~~~~ix

  // ### it is at least 3 characters long.
  assert (ix >= 2);
  unsigned char c2 = str[ix-2];
  const bool b2_7 = c2 & (1 << 7);
  const bool b2_6 = c2 & (1 << 6);
  const bool b2_5 = c2 & (1 << 5);
  const bool b2_4 = c2 & (1 << 4);
  assert(b2_7);  
  // 1xxxxxx; 10xxxxxxx; 10xxxxxx
  //                     ^~~~~ix
  if (b2_6) {
    assert(b2_5);
    assert(b2_4 == 0);
    // 1110xxxx; 10xxxxxxx; 10xxxxxx
    //                      ^~~~~ix
    return 3;
  }
  assert(b1_6 == 0);
  // 10xxxxx; 10xxxxxxx; 10xxxxxx
  //                     ^~~~~ix
  
  // ### it must be 4 characters long.
  assert(ix >= 3);
  unsigned char c3 = str[ix-3];
  const bool b3_7 = c3 & (1 << 7);
  const bool b3_6 = c3 & (1 << 6);
  const bool b3_5 = c3 & (1 << 5);
  const bool b3_4 = c3 & (1 << 4);
  const bool b3_3 = c3 & (1 << 3);
  assert(b3_7);  
  assert(b3_6);  
  assert(b3_5);  
  assert(b3_4);  
  assert(b3_3 == 0);  
  // 11110xxx; 10xxxxx; 10xxxxxxx; 10xxxxxx
  //                                ^~~~~ix
  return 4;
};

