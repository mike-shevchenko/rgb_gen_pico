// Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/
// Modified by Mike Shevchenko.

#pragma once

/**@file
 * Various utilities. Used by other nx_kit components.
 *
 * This unit can be compiled in the context of any C++ project.
 *
 * NOTE: It was part of nx_kit library. This file is stipped down for embedded use.
 */

#include <cstring>
#include <cstdarg>
#include <stdint.h>
#include <string>

#if !defined(NX_KIT_API)
    #define NX_KIT_API /*empty*/
#endif

namespace nx {
namespace kit {
namespace utils {

//-------------------------------------------------------------------------------------------------
// Strings.

inline bool isAsciiPrintable(int c)
{
    return c >= 32 && c <= 126;
}

inline bool isSpaceOrControlChar(char c)
{
    // NOTE: Chars 128..255 should be treated as non-whitespace, thus, isprint() will not do.
    return (((unsigned char) c) <= 32) || (c == 127);
}

/**
 * Decodes a string encoded using C/C++ string literal rules: enquoted, potentially containing
 * escape sequences. Supports concatenation of consecutive literals, thus, fully compatible with
 * strings encoded by nx::kit::utils::toString().
 *
 * @param outErrorMessage In case of any error in the encoded string, the function attempts to
 *     recover using the most obvious way, still producing the result, and reports all such cases
 *     via this argument if it is not null.
 */
NX_KIT_API std::string decodeEscapedString(
    const std::string& s, std::string* outErrorMessage = nullptr);

/**
 * Converts a value to its report-friendly text representation; for strings it being a quoted and
 * C-style-escaped string. Non-printable chars in a string are represented as hex escape sequences
 * like `\xFF""` - note that the two quotes after it are inserted to indicate the end of the hex
 * number, because according to the C/C++ standards, `\x` consumes as much hex digits as possible.
 */
template<typename T>
std::string toString(T value);

/** Used by format(). */
NX_KIT_API std::string vformat(const std::string& fmt, va_list args);

/** ATTENTION: std::string is not supported as one of `args`, and will cause undefined behavior. */
inline std::string format(const std::string& fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    const std::string result = vformat(fmt, args);
    va_end(args);
    return result;
}

NX_KIT_API bool fromString(const std::string& s, int* value);
NX_KIT_API bool fromString(const std::string& s, uint32_t* value);
NX_KIT_API bool fromString(const std::string& s, uint64_t* value);
NX_KIT_API bool fromString(const std::string& s, double* value);
NX_KIT_API bool fromString(const std::string& s, float* value);
NX_KIT_API bool fromString(const std::string& s, bool* value);

NX_KIT_API void stringReplaceAllChars(std::string* s, char sample, char replacement);
NX_KIT_API void stringInsertAfterEach(std::string* s, char sample, const char* insertion);
NX_KIT_API void stringReplaceAll(
    std::string* s, const std::string& sample, const std::string& replacement);

// TODO: Remove when migrating to C++20 - it has std::string::starts_with()/ends_with().
NX_KIT_API bool stringStartsWith(const std::string& s, const std::string& prefix);
NX_KIT_API bool stringEndsWith(const std::string& s, const std::string& suffix);

// TODO: Remove when migrating to C++23 - it has std::string::contains().
NX_KIT_API bool stringContains(const std::string& s, const std::string& substring);

NX_KIT_API std::string trimString(const std::string& s);

/** Converts ASCII characters from the input string to the upper case. */
NX_KIT_API std::string toUpper(const std::string& str);

//-------------------------------------------------------------------------------------------------
// Implementation.

// The order of overloads below is important - it defines which will be chosen by inline functions.
NX_KIT_API std::string toString(bool b);
NX_KIT_API std::string toString(const void* ptr);
inline std::string toString(void* ptr) { return toString(const_cast<const void*>(ptr)); }
inline std::string toString(std::nullptr_t ptr) { return toString((const void*) ptr); }
inline std::string toString(uint8_t i) { return toString((int) i); } //< Avoid matching as char.
inline std::string toString(int8_t i) { return toString((int) i); } //< Avoid matching as char.
NX_KIT_API std::string toString(char c);
NX_KIT_API std::string toString(const char* s);
inline std::string toString(char* s) { return toString(const_cast<const char*>(s)); }
NX_KIT_API std::string toString(wchar_t c);
NX_KIT_API std::string toString(const wchar_t* w);
inline std::string toString(wchar_t* w) { return toString(const_cast<const wchar_t*>(w)); }

// std::string can contain '\0' inside, hence a dedicated implementation.
NX_KIT_API std::string toString(const std::string& s);
NX_KIT_API std::string toString(const std::wstring& w);

/**
 * For unknown types, use std::to_string().
 *
 * NOTE: In the original nx_kit, it was implemented via ostringstream, but for embedded use such
 * approach takes a lot more code memory (~600 KB), so changed to std::to_string().
 */
template<typename T>
std::string toString(T value)
{
    return std::to_string(value);
}

template<typename P>
std::string toString(P* ptr)
{
    return toString((const void*) ptr);
}

} // namespace utils
} // namespace kit
} // namespace nx
