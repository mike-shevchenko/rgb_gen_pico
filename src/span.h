#pragma once

#include <type_traits>
#include <utility>

#include <pico/stdlib.h>

#include "debug.h"

// Asserts the access.
template <typename T>
class Span {
 private:
  template <typename ConstT>
  requires std::is_const_v<ConstT>
  using MutableT = std::remove_const_t<ConstT>;

 public:
  __force_inline Span(T* data, int size) : data_(data), size_(size) {}

  __force_inline const T& operator[](int index) const {
    ASSERT_CMP(index, <, size_);
    return data_[index];
  }

  __force_inline T& operator[](int index) { return const_cast<T&>(std::as_const(*this)[index]); }

  // Optimized bulk operation, checking the range once.
  template <typename DestType, typename TransformFunc>
  __force_inline void CopyTo(
      DestType* dest, int index, int count, TransformFunc transform) const {
    ASSERT_CMP(index + count, <=, size_);
    for (int i = 0; i < count; ++i) {
      dest[i] = transform(data_[index + i]);
    }
  }

  // Allows to write a non-const span getter calling the const one.
  template <typename ConstT = T>
  requires std::is_const_v<ConstT>
  __force_inline Span<MutableT<ConstT>> AsMutable() const {
    return Span<MutableT<ConstT>>(const_cast<MutableT<ConstT>*>(data_), size_);
  }

  __force_inline int size() const { return size_; }
  __force_inline T* data() { return data_; }
  __force_inline const T* data() const { return data_; }

 private:
  T* data_;
  int size_;
};
