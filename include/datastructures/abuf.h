#pragma once
#include "mathutil.h"
#include <string>

struct abuf {
  abuf() = default;
  ~abuf();

  static abuf from_steal_str(char *str);
  static abuf from_copy_str(const char *str);
  static abuf from_steal_buf(char *buf, int len);
  static abuf from_copy_buf(const char *buf, int len);

  abuf &operator = (const abuf &other);
  abuf(const abuf &other);

  void appendbuf(const char *s, int slen);

  void appendbuf(const abuf *other);
  // append a UTF-8 codepoint.
  void appendCodepoint(const char *codepoint);

  void prependbuf(const char *s, int slen);
  void prependbuf(const abuf *other);
  void prependCodepoint(const char *codepoint);

  // TODO: rename API
  Size<Byte> getCodepointBytesAt(Ix<Codepoint> i) const;

  char getByteAt(Ix<Byte> i) const;


  // insert a single codepoint.
  void insertCodepointBefore(Size<Codepoint> at, const char *codepoint);

  void delCodepointAt(Ix<Codepoint> at);
  // append a sequence of n UTF-8 codepoints.
  void appendCodepoints(const char *codepoint, int n);
  void appendChar(char c);

  // take the substring of [start,start+len) and convert it to a string.
  // pointer returned must be free.
  char *to_string_start_len(int start, int slen) const;

  // take substring [start, buflen).
  char *to_string_from_start_ix(int startix) const;

  // take substring [0, slen)
  char *to_string_len(int slen) const ;

  // TODO: rename to 'to_c_str()'
  // convert buffer to string.
  char *to_string() const;

  std::string to_std_string() const;

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int find_sub_buf(const char *findbuf, int findbuf_len, int begin_ix) const;

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int find_substr(const char *findstr, int begin_ix) const;

  // Return first index `i >= begin_ix` such that `buf[i] = c`.
  // Return `-1` otherwise.
  int find_char(char c, int begin_ix) const;

  // append a string onto this string.
  void appendstr(const char *s);

  // append a format string onto this string. Truncate to length 'len'.
  void appendfmtstr(int len, const char *fmt, ...);


  // drop a prefix of the buffer. If `drop_len = 0`, then this operation is a
  // no-op.
  void dropNBytesMut(int drop_len);

  int len() const;

  const char *buf() const;

  // if dirty, return `true` and reset the dirty state of `abuf`.
  bool whenDirty();


  const char *getCodepointFromRight(Ix<Codepoint> ix) const;

  bool operator == (const abuf &other) const;
  
  Size<Byte> nbytes() const;

  Size<Codepoint> ncodepoints() const;

  const char *debugToString() const;

  const char *getCodepoint(Ix<Codepoint> ix) const;

  // TODO: think about why we need the other version.
  // 'Clearly', this version is correct, since even when 'ix = len',
  // we will return a valid pointer (end of list).
  const char *getCodepoint(Size<Codepoint> sz) const;


  // get the raw _buf. While functionally equivalent to
  // getCodepoint, this gives one more license to do things like `memcpy`
  // and not have it look funny.
  const char *getRawBytesPtrUnsafe() const ;

  Size<Byte> getBytesTill(Size<Codepoint> n) const;

  // TODO: move this out.
  int cxToRx(Size<Codepoint> cx) const;

  // it is size, since we can ask to place the data at the *end* of the string, past the
  // final.
  void insertByte(Size<Codepoint> at, int c);

  // set the data.
  // TODO: think if we should expose _buf API.
  // TODO: force copy codepoint by codepoint.
  void setBytes(const char *buf, int len);

  abuf takeNBytes(Size<Byte> bytes) const;
  // truncate to `ncodepoints_new` codepoints.
  void truncateNCodepoints(Size<Codepoint> ncodepoints_new);

protected:
  char *_buf = nullptr;
  int _len = 0;
  bool _is_dirty = true;

};

