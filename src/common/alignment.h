// This file is under the public domain.

#pragma once

#include <cstddef>
#include <type_traits>

namespace Common {

template <typename T>
constexpr T AlignUp(T value, std::size_t size) {
    static_assert(std::is_integral_v<T>, "T must be an integral value.");
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");
    if ((size & (size - 1)) == 0) {
        return static_cast<T>((value + size - 1) & (~(size - 1)));
    }
    return static_cast<T>((value + size - 1) / size * size);
}

template <typename T>
constexpr T AlignDown(T value, std::size_t size) {
    static_assert(std::is_integral_v<T>, "T must be an integral value.");
    static_assert(std::is_unsigned_v<T>, "T must be an unsigned value.");

    if ((size & (size - 1)) == 0) {
        return static_cast<T>((value) & (~(size - 1)));
    }
    return static_cast<T>((value) / size * size);
}

} // namespace Common
