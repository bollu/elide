#pragma once
#include "datastructures/abuf.h"
#include "datastructures/utf8.h"
#include "definitions/nspacespertab.h"
#include <stdarg.h>
#include <stdio.h>
#include <string.h>

abuf::~abuf() { free(_buf); }

abuf abuf::from_steal_str(char *str) {
    abuf out;
    out._buf = str;
    out._len = strlen(str);
    return out;
  }

abuf abuf::from_copy_str(const char *str) {
    abuf out;
    out._buf = strdup(str);
    out._len = strlen(str);
    return out;
  }

abuf abuf::from_steal_buf(char *buf, int len) {
    abuf out;
    out._buf = buf;
    out._len = len;
    return out;
  }

abuf abuf::from_copy_buf(const char *buf, int len) {
    abuf out;
    out.appendbuf(buf, len);
    return out;
  }

  abuf &abuf::operator = (const abuf &other) {
    _len = other._len;

    if (_len == 0) {
      _buf = nullptr;
      this->_is_dirty = other._is_dirty;
      return *this;
    }
    free(_buf);
    _buf = (char*)malloc(sizeof(char) * _len);
    if (_len > 0) {
      memcpy(_buf, other._buf, _len);
    }
    this->_is_dirty = other._is_dirty;
    return *this;
  }
  abuf::abuf(const abuf &other) {
    *this = other;
  }

  void abuf::appendbuf(const char *s, int slen) {
    assert(slen >= 0 && "negative length!");
    if (slen == 0) { return; }
    this->_buf = (char *)realloc(this->_buf, this->_len + slen);
    assert(this->_buf && "unable to append string");
    memcpy(this->_buf + this->_len, s, slen);
    this->_len += slen;
    this->_is_dirty = true;
  }

  void abuf::appendbuf(const abuf *other) {
    appendbuf(other->_buf, other->_len);
    this->_is_dirty = true;
  }


  // append a UTF-8 codepoint.
  void abuf::appendCodepoint(const char *codepoint) {
    const int len = utf8_next_code_point_len(codepoint);
    this->appendbuf(codepoint, len);
    this->_is_dirty = true;
  }

  void abuf::prependbuf(const char *s, int slen) {
    assert(slen >= 0 && "negative length!");
    if (slen == 0) { return; }
    this->_buf = (char *)realloc(this->_buf, this->_len + slen);
    
  // shift data forward.
    for(int i = 0; i < _len; ++i) {
      this->_buf[this->_len - 1 - i] = this->_buf[this->_len + slen - 1 - i];
    }

    for(int i = 0; i < slen; ++i) {
      this->_buf[i] = s[i];
    }

    this->_len += slen;
    this->_is_dirty = true;
  }

  void abuf::prependbuf(const abuf *other) {
    prependbuf(other->_buf, other->len());
    this->_is_dirty = true;
  }
  void abuf::prependCodepoint(const char *codepoint) {
    const int len = utf8_next_code_point_len(codepoint);
    this->prependbuf(codepoint, len);
    this->_is_dirty = true;
  }

  // TODO: rename API
  Size<Byte> abuf::getCodepointBytesAt(Ix<Codepoint> i) const {
    return Size<Byte>(utf8_next_code_point_len(getCodepoint(i)));
  }

  char abuf::getByteAt(Ix<Byte> i) const {
    assert(Ix<Byte>(0) <= i);
    assert(i < this->len());
    return this->_buf[i.ix];
  }


  // insert a single codepoint.
  void abuf::insertCodepointBefore(Size<Codepoint> at, const char *codepoint) {
    assert(at.size >= 0);
    assert(at.size <= this->_len);

    // TODO: refactor by changing type to `abuf`.
    Size<Byte> n_bufUptoAt = Size<Byte>(0);
    for(Ix<Codepoint> i(0); i < at; i++)  {
      n_bufUptoAt += this->getCodepointBytesAt(i);
    }

    const Size<Byte> nNew_buf(utf8_next_code_point_len(codepoint));
    this->_buf = (char*)realloc(this->_buf, this->_len + nNew_buf.size);
      
    for(int oldix = this->_len - 1; oldix >= n_bufUptoAt.size; oldix--) {
      // push _buf from `i` into `i + nNew_buf`.
      this->_buf[oldix + nNew_buf.size] = this->_buf[oldix];
    }    

    // copy new _buf into into location.
    for(int i = 0; i < nNew_buf.size; ++i)  {
      this->_buf[n_bufUptoAt.size + i] = codepoint[i];
    }
    this->_len += nNew_buf.size;
    this->_is_dirty = true;

  }

  void abuf::delCodepointAt(Ix<Codepoint> at) {
    // TODO: refactor by changing type to `abuf`.
    Size<Byte> startIx = Size<Byte>(0);
    for(Ix<Codepoint> i(0); i < at; i++)  {
      startIx += this->getCodepointBytesAt(i);
    }

    const Size<Byte> ntoskip = this->getCodepointBytesAt(at);
    
    for(int i = startIx.size; i < this->_len - ntoskip.size; i++) {
      this->_buf[i] = this->_buf[i + ntoskip.size];
    }    
    this->_len -= ntoskip.size;
    // resize to eliminate leftover.
    this->_buf = (char *)realloc(this->_buf, this->_len);
    this->_is_dirty = true;
  }


  // append a sequence of n UTF-8 codepoints.
  void abuf::appendCodepoints(const char *codepoint, int n) {
    int delta = 0;
    for(int i = 0; i < n; ++i)  {
      delta += utf8_next_code_point_len(codepoint);
      this->appendCodepoint(codepoint + delta);
    }
    this->_is_dirty = true;
  }

  void abuf::appendChar(char c) {
    this->appendbuf(&c, 1);
    this->_is_dirty = true;
  }

  // take the substring of [start,start+len) and convert it to a string.
  // pointer returned must be free.
  char *abuf::to_string_start_len(int start, int slen) const {
    slen = clamp0u<int>(slen, this->len() - start); 
      // std::max<int>(0, std::min<int>(slen, this->_len - start));
    assert(slen >= 0);
    char *out = (char *)calloc(slen + 1, sizeof(char));
    if (this->_buf != NULL) {
      memcpy(out, this->_buf + start, slen);
    }
    return out;
  }

  // take substring [start, buflen).
  char *abuf::to_string_from_start_ix(int startix) const {
    return to_string_start_len(startix, this->_len);
  }

  // take substring [0, slen)
  char *abuf::to_string_len(int slen) const {
    return to_string_start_len(0, slen);
  }

  // TODO: rename to 'to_c_str()'
  // convert buffer to string.
  char *abuf::to_string() const {
    return to_string_start_len(0, this->_len);
  }

  std::string abuf::to_std_string() const {
    return std::string(this->_buf, this->_buf + this->_len);
  }

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int abuf::find_sub_buf(const char *findbuf, int findbuf_len, int begin_ix) const {
    for (int i = begin_ix; i < this->_len; ++i) {
      int match_len = 0;
      while (i + match_len < this->_len && match_len < findbuf_len) {
        if (this->_buf[i + match_len] == findbuf[match_len]) {
          match_len++;
        } else {
          break;
        }
      }
      // we matched, return index.
      if (match_len == findbuf_len) { return i; }
    };
    // no match, return -1;
    return -1;
  };

  // Return first index `i >= begin_ix` such that `buf[i:i+len] = s[0:len]`.
  // Return `-1` otherwise.
  int abuf::find_substr(const char *findstr, int begin_ix) const {
    return find_sub_buf(findstr, strlen(findstr), begin_ix);
  }

  // Return first index `i >= begin_ix` such that `buf[i] = c`.
  // Return `-1` otherwise.
  int abuf::find_char(char c, int begin_ix) const { return find_sub_buf(&c, 1, begin_ix); }


  // append a string onto this string.
  void abuf::appendstr(const char *s) { 
    appendbuf(s, strlen(s));
    this->_is_dirty = true;
  }

  // append a format string onto this string. Truncate to length 'len'.
  void abuf::appendfmtstr(int len, const char *fmt, ...) {
    va_list args;
    va_start(args, fmt); 
    char *buf = (char*)malloc(sizeof(char) * len);
    vsnprintf(buf, len, fmt, args);
    va_end(args);
    appendstr(buf);
    free(buf); 
    this->_is_dirty = true;
  }


  // drop a prefix of the buffer. If `drop_len = 0`, then this operation is a
  // no-op.
  void abuf::dropNBytesMut(int drop_len) {
    assert(drop_len >= 0);
    char *bnew = (char *)malloc(sizeof(char) * (_len - drop_len));
    memcpy(bnew, this->_buf + drop_len, this->_len - drop_len);
    free(this->_buf);
    this->_buf = bnew;
    this->_len -= drop_len;
    this->_is_dirty = true;
  }

  int abuf::len() const {
    return this->_len;
  }

  const char *abuf::buf() const {
    return this->_buf;
  }

  // if dirty, return `true` and reset the dirty state of `abuf`.
  bool abuf::whenDirty() {
    const bool out = this->_is_dirty;
    this->_is_dirty = false;
    return out;
  }


  const char *abuf::getCodepointFromRight(Ix<Codepoint> ix) const {
    assert(ix < this->ncodepoints());
    return this->getCodepoint(this->ncodepoints().mirrorIx(ix));
  }

  bool abuf::operator == (const abuf &other) const {
    if (this->_len != other._len) { return false; }
    for(int i = 0; i < this->_len; ++i) {
      if (this->_buf[i] != other._buf[i]) { return false; }
    } 
    return true;
  }
  
  Size<Byte> abuf::nbytes() const {
    return Size<Byte>(this->_len);
  }


  Size<Codepoint> abuf::ncodepoints() const {
    int count = 0;
    int ix = 0;
    while(ix < this->_len) {
      ix += utf8_next_code_point_len(this->_buf + ix);
      count++;
    }
    assert(ix == this->_len);
    return Size<Codepoint>(count);
  }

  const char *abuf::debugToString() const {
    char *str = (char*)malloc(this->nbytes().size + 1);
    for(int i = 0; i < this->nbytes().size; ++i) {
      str[i] = this->_buf[i];
    }
    str[this->nbytes().size] = '\0';
    return str;
  }

  const char *abuf::getCodepoint(Ix<Codepoint> ix) const {
    assert(ix < this->ncodepoints());
    int delta = 0;
    for(Ix<Codepoint> i(0); i < ix; i++) {
      delta += utf8_next_code_point_len(this->_buf + delta);
    }
    return this->_buf + delta;
  }

  // TODO: think about why we need the other version.
  // 'Clearly', this version is correct, since even when 'ix = len',
  // we will return a valid pointer (end of list).
  const char *abuf::getCodepoint(Size<Codepoint> sz) const {
    assert(sz <= this->ncodepoints());
    int delta = 0;
    for(Ix<Codepoint> i(0); i < sz; i++) {
      delta += utf8_next_code_point_len(this->_buf + delta);
    }
    return this->_buf + delta;
  }


  // get the raw _buf. While functionally equivalent to
  // getCodepoint, this gives one more license to do things like `memcpy`
  // and not have it look funny.
  const char *abuf::getRawBytesPtrUnsafe() const {
    return this->_buf;
  }

  // // TODO: rename API
  // Size<Byte> getCodepointBytesAt(Ix<Codepoint> i) const {
  //   return Size<Byte>(utf8_next_code_point_len(getCodepoint(i)));
  // }
  Size<Byte> abuf::getBytesTill(Size<Codepoint> n) const {
    Size<Byte> out(0);
    for(Ix<Codepoint> i(0); i < n; ++i)  {
      out += getCodepointBytesAt(i);
    }
    return out;
  }


  int abuf::cxToRx(Size<Codepoint> cx) const {
    assert(cx <= this->ncodepoints());
    int rx = 0;
    char *p = this->_buf;
    for (Ix<Codepoint> j(0); j < cx; ++j) {
      if (*p == '\t') {
        rx += NSPACES_PER_TAB - (rx % NSPACES_PER_TAB);
      } else {
        rx += 1; // just 1.
      }
      p += this->getCodepointBytesAt(j).size;
    }
    return rx;
  }

  // it is size, since we can ask to place the data at the *end* of the string, past the
  // final.
  void abuf::insertByte(Size<Codepoint> at, int c) {
    assert(at.size >= 0);
    assert(at.size <= this->_len);
    _buf = (char *)realloc(_buf, this->_len + 1);

    Size<Byte> byte_at(0);
    for(Ix<Codepoint> i(0); i < at; ++i) {
      byte_at += this->getCodepointBytesAt(i);
    }

    for(int i = this->_len; i >= byte_at.size+1; i--) {
      this->_buf[i] = this->_buf[i - 1];
    }    
    _buf[byte_at.size] = c;
    this->_len += 1;
    this->_is_dirty = true;
  }


  // set the data.
  // TODO: think if we should expose _buf API.
  // TODO: force copy codepoint by codepoint.
  void abuf::setBytes(const char *buf, int len) {
    this->_len = len;
    _buf = (char *)realloc(_buf, sizeof(char) * this->_len);
    for(int i = 0; i < len; ++i) {
      _buf[i] = buf[i];
    }
    this->_is_dirty = true;

  }

  abuf abuf::takeNBytes(Size<Byte> bytes) const {
    assert(bytes.size >= 0);
    assert(bytes.size <= this->_len);
    abuf buf;
    buf._len = bytes.size;
    buf._buf = (char*)calloc(sizeof(char), buf._len);
    buf._is_dirty  = true;
    for(int i = 0; i < buf._len; ++i) {
      buf._buf[i] = this->_buf[i];
    }
    return buf;
  };

  // truncate to `ncodepoints_new` codepoints.
  void abuf::truncateNCodepoints(Size<Codepoint> ncodepoints_new) {
    assert(ncodepoints_new <= this->ncodepoints());
    Size<Byte> nbytes(0);
    for(Ix<Codepoint> i(0); i < ncodepoints_new; i++)  {
      nbytes += this->getCodepointBytesAt(i);
    }
    this->_buf = (char*)realloc(this->_buf, nbytes.size);
    this->_len = nbytes.size;
    this->_is_dirty = true;
  }

