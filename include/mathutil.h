static int clamp(int lo, int val, int hi) {
  return std::min<int>(std::max<int>(lo, val), hi);
}

static int clamplu(int lo, int val, int hi) {
  return clamp(lo, val, hi);
}
static int clampl(int lo, int val) {
  return std::max<int>(lo, val);
};
static int clampu(int val, int hi) {
  return std::min<int>(val, hi);
};

static int clamp0(int val) {
  return clampl(0, val);
};

static int clamp0u(int val, int hi) {
  return clamplu(0, val, hi);
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

  bool operator <(const Size<T> &other) const;

  bool operator == (const Ix<T> &other) {
    return this->ix ==  other.ix;
  }

  bool operator > (const Ix<T> &other) {
    return this->ix > other.ix;
  }

  bool is_inbounds(Size<T> size) const;
  bool is_inbounds_or_end(Size<T> size) const;
};

template<typename T>
struct Size {
  int size = 0;
  explicit Size() = default;
  explicit Size(int size) : size(size) {};
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

  Size<T> operator - (const Size<T> &other) const {
    int out = (this->size - other.size);
    assert(out >= 0);
    return out;
  }

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


