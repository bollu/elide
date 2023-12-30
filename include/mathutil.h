#include <functional>

template<typename T>
static int clamp(T lo, T val, T hi) {
  return std::min<T>(std::max<T>(lo, val), hi);
}

template<typename T>
static T clamplu(T lo, T val, T hi) {
  return clamp<T>(lo, val, hi);
}

template<typename T>
static T clampl(T lo, T val) {
  return std::max<T>(lo, val);
};

template<typename T>
static T clampu(T val, T hi) {
  return std::min<T>(val, hi);
};

template<typename T>
static T clamp0(T val) {
  return clampl<T>(T(0), val);
};

template<typename T>
static T clamp0u(T val, T hi) {
  return clamplu<T>(0, val, hi);
}


struct Byte{}; // Ix<Byte>.
struct Codepoint{}; // Ix<Grapheme>.

template<typename T>
struct Size;

template<typename T>
struct Ix;

template<typename T>
struct Ix {
  int ix = 0;
  explicit Ix() = default;
  explicit Ix(int ix) { this->ix = ix; };
  Ix &operator = (const Ix<T> &other) {
    this->ix = other.ix;
    return *this;
  }

  Ix(const Ix<T> &other) {
    *this = other;
  }

  Ix<T> operator++(int) {
    // postfix
    Ix<T> copy(*this);
    this->ix += 1;
    return copy;
  }

  Ix<T> &operator++() {
    // prefix
    this->ix += 1;
    return *this;
  }

  Ix<T> operator--(int) const {
    // postfix
    return Ix<T>(this->ix - 1);
  }

  bool operator <(const Ix<T> &other) const {
    return this->ix < other.ix;
  }

  bool operator <=(const Ix<T> &other) const {
    return this->ix <= other.ix;
  }

  bool operator > (const Ix<T> &other) const {
    return this->ix > other.ix;
  }

  bool operator <(const Size<T> &other) const;

  bool operator == (const Ix<T> &other) {
    return this->ix ==  other.ix;
  }

  bool operator > (const Ix<T> &other) {
    return this->ix > other.ix;
  }

  bool is_inbounds(Size<T> size) const;
  bool is_inbounds_or_end(Size<T> size) const;
  int distance(Ix<T> other) const {
    if(other.ix < this->ix) {
      return this->ix - other.ix;
    } else {
      assert(this->ix <= other.ix);
      return other.ix - this->ix;
    }
  }

};

template<typename T>
struct Size {
  int size = 0;
  explicit Size() = default;
  Size(int size) : size(size) {};
  Size &operator = (const Size<T> &other) {
    this->size = other.size;
    return *this;
  }
  Size(const Size<T> &other) {
    *this = other;
  }

  Size<T> next() const { // return next size.
    return Size<T>(this->size + 1);
  }

  Size<T> prev() const { // return next size.
    assert(this->size - 1 >= 0);
    return Size<T>(this->size - 1);
  }
  
  Ix<T> toIx() const {
    return Ix<T>(this->size);
  }

  // return largest index that can index
  // an array of size 'this'.
  Ix<T> largestIx() const {
    assert(this->size > 0);
    return Ix<T>(this->size - 1);
  }

  // returns 'true' if ix at the end of this range.
  bool isEnd(Ix<T> ix) const {
    return this->size == ix.ix;
  }

  // mirrors the index from the left to be its flipped version from the right.
  // sends:
  //   0 -> n - 1
  //   1 -> n - 2
  //   ...
  //   n - 1 -> 0
  Ix<T> mirrorIx(Ix<T> ix) const {
    return Ix<T>(this->largestIx().ix - ix.ix);
  }

  // convert from size T to size S.
  // Can covert from e.g. Codepoints to Bytes if one is dealing with ASCII,
  // since every ASCII object takes 1 codepoint.
  template<typename S>
  Size<S> unsafe_cast() const {
    return Size<S>(this->size);
  }

  void operator += (const Size<T> &other) {
    this->size += other.size;
  }

  Size<T> operator + (const Size<T> &other) const {
    return Size<T>(this->size + other.size);
  }

  // subtract with clamping down to zero.
  Size<T> sub0(const Size<T> &other) const {
    return Size<T>(clamp0<int>(this->size - other.size));
  }

  // Size<T> operator - (const Size<T> &other) const {
  //   int out = (this->size - other.size);
  //   assert(out >= 0);
  //   return out;
  // }

  bool operator < (const Size<T> &other) const {
    return this->size < other.size;
  }

  bool operator <= (const Size<T> &other) const {
    return this->size <= other.size;
  }

  bool operator > (const Size<T> &other) const {
    return this->size > other.size;
  }

  bool operator >= (const Size<T> &other) const {
    return this->size >= other.size;
  }



  bool operator == (const Size<T> &other) {
    return this->size ==  other.size;
  }

  Size<T> operator++(int) {
    // postfix
    Size<T> copy(*this);
    this->size += 1;
    return copy;
  }

  Size<T> &operator++() {
    // prefix
    this->size += 1;
    return *this;
  }

  Size<T> operator--(int) {
    // postfix
    Size<T> copy(*this);
    this->size -= 1;
    return copy;
  }

};

template<typename T>
bool Ix<T>::operator <(const Size<T> &other) const {
  return ix < other.size;
}

// Bounded int, with bounds `[0, MAX]`.
template<typename T> // tagged with Size/Ix<Codepoint/Byte>
struct bint {
  int lo;
  int hi;
  int val;
  
  static bint combine(bint x, bint y, std::function<int(int, int)> f) {
    return bint(std::max<int>(x.lo, y.lo), 
    	f(x.val, y.val),
	std::min<int>(x.hi, y.hi)); 
  }

  bint(int val, int hi) : lo(0), val(val), hi(hi) {
    val = std::min<int>(std::max<int>(lo, val), hi);
  }

  bint(int lo, int val, int hi) : lo(lo), val(val), hi(hi) {
    val = std::min<int>(std::max<int>(lo, val), hi);
  }

  operator int() const {
    return val;
  }
};


