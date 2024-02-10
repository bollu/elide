#pragma once
#include <vector>

template <typename T>
struct Zipper {
public:
    int size() const
    {
        return _ts.size();
    }

    T* getFocus()
    {
        if (_ts.size() == 0) {
            return nullptr;
        } else {
            assert(this->_curIx >= 0);
            assert(this->_curIx < this->_ts.size());
            return &_ts[this->_curIx];
        }
    }

    void push_back(const T& t)
    {
        _push_back_no_duplicates(t);
        this->_curIx = _ts.size() - 1;
    }

    void left()
    {
        if (this->_ts.size() == 0) {
            return;
        }
        this->_curIx = clamp0<int>(this->_curIx - 1);
    }

    void right()
    {
        if (this->_ts.size() == 0) {
            return;
        }
        this->_curIx = clampu<int>(this->_curIx + 1, _ts.size() - 1);
    }

    int getIx() const
    {
        return _curIx;
    }

private:
    void _push_back_no_duplicates(const T& t)
    {
        if (_ts.size() > 0 && t == _ts[_ts.size() - 1]) {
            return;
        }
        _ts.push_back(t);
    }
    std::vector<T> _ts;
    int _curIx = -1;
};