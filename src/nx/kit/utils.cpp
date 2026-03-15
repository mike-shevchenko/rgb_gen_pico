// Copyright 2018-present Network Optix, Inc. Licensed under MPL 2.0: www.mozilla.org/MPL/2.0/
// Modified by Mike Shevchenko.

#include "utils.h"

#include <climits>

namespace nx {
namespace kit {
namespace utils {

/**
 * Appends an error message to std::string* errorMessage, concatenating via a space if
 * *errorMessage was not empty. Does nothing if errorMessage is null.
 *
 * NOTE: This is a macro to avoid composing the error message if errorMessage is null.
 */
#define ADD_ERROR_MESSAGE(ERROR_MESSAGE) do \
{ \
    if (errorMessage) \
    { \
        if (!errorMessage->empty()) \
            *errorMessage += " "; \
        *errorMessage += (ERROR_MESSAGE); \
    } \
} while (0)

/**
 * Parses the octal escape sequence at the given position.
 * @param p Pointer to a position right after the backslash; must contain an octal digit. After the
 *     call, the pointer is moved to the position after the escape sequence.
 * @param errorMessage A string which the error message, if any, will be appended to, if not null.
 * @return Whether the given position represents an octal escape sequence.
 */
static std::string decodeOctalEscapeSequence(const char** pp, std::string* errorMessage)
{
    // According to the C and C++ standards, up to three octal digits are recognized.
    // If the resulting number is greater than 255, it has been decided to consider it an error
    // rather than a Unicode character (the standard leaves it to the implementation).

    int code = 0;
    int digitCount = 0;
    while (**pp >= '0' && **pp <= '7')
    {
        ++digitCount;
        if (digitCount > 3)
            break;
        code = (code << 3) + (**pp - '0');
        ++(*pp);
    }

    if (code > 255 && errorMessage)
    {
        ADD_ERROR_MESSAGE(nx::kit::utils::format(
            "Octal escape sequence does not fit in one byte: %d.", code));
    }

    return std::string(1, (char) code);
}

/**
 * @return -1 if the char is not a hex digit.
 */
static int toHexDigit(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

/**
 * Parses the "\x" escape sequence.
 * @param p Pointer to a position right after "\x". After the call, the pointer is moved to the
 *     position after the escape sequence.
 * @param errorMessage A string which the error message, if any, will be appended to, if not null.
 */
static std::string decodeHexEscapeSequence(const char** pp, std::string* errorMessage)
{
    // According to the C and C++ standards, as much hex digits are part of the sequence as can be
    // found, and if the resulting number does not fit into char, it's an implementation decision -
    // we have decided to produce an error rather than a Unicode character.

    uint8_t code = 0;
    bool fitsInByte = true;

    int hexDigit = toHexDigit(**pp);

    if (hexDigit < 0)
    {
        ADD_ERROR_MESSAGE("Missing hex digits in the hex escape sequence.");
        return "";
    }

    while (hexDigit >= 0)
    {
        if (code > 0x0F)
            fitsInByte = false;
        code = (code << 4) + (uint8_t) hexDigit;
        ++(*pp);
        hexDigit = toHexDigit(**pp);
    }

    if (!fitsInByte)
        ADD_ERROR_MESSAGE("Hex escape sequence does not fit in one byte.");

    return std::string(1, (char) code);
}

/**
 * Parses the "\x" escape sequence.
 * @param p Pointer to a position right after "\". After the call, the pointer is moved to the
 *     position after the escape sequence.
 * @param errorMessage A string which the error message, if any, will be appended to, if not null.
 */
static std::string decodeEscapeSequence(const char** pp, std::string* errorMessage)
{
    const char escaped = **pp;

    if (escaped >= '0' && escaped <= '7')
        return decodeOctalEscapeSequence(pp, errorMessage);
    ++(*pp); //< Now points after the first char after the backslash.

    switch (escaped)
    {
        case '\'': case '\"': case '\?': case '\\': return std::string(1, escaped);
        case 'a': return "\a";
        case 'b': return "\b";
        case 'f': return "\f";
        case 'n': return "\n";
        case 'r': return "\r";
        case 't': return "\t";
        case 'v': return "\v";
        case 'x': return decodeHexEscapeSequence(pp, errorMessage);
        // NOTE: Unicode escape sequences `\u` and `\U` are not supported because they
        // would require generating UTF-8 sequences.
        case '\0':
            --(*pp); //< Move back to point to '\0'.
            ADD_ERROR_MESSAGE("Missing escaped character after the backslash.");
            return "\\";
        default:
            ADD_ERROR_MESSAGE(
                "Invalid escaped character " + toString(escaped) + " after the backslash.");
            return std::string(1, escaped);
    }
}

NX_KIT_API std::string decodeEscapedString(const std::string& s, std::string* errorMessage)
{
    if (errorMessage)
        *errorMessage = "";

    std::string result;
    result.reserve(s.size()); //< Most likely, the result will have almost the size of the source.

    const char* p = s.c_str();
    if (*p != '"')
    {
        ADD_ERROR_MESSAGE("The string does not start with a quote.");
        return s; //< Return the input string as-is.
    }
    ++p;

    for (;;)
    {
        const char c = *p;
        switch (c)
        {
            case '\0':
                ADD_ERROR_MESSAGE("Missing the closing quote.");
                return result;
            case '"': //< Support C-style concatenation of consecutive enquoted strings.
                ++p;
                while (*p > 0 && *p <= 32) //< Skip all whitespace between the quotes.
                    ++p;
                if (*p == '\0')
                    return result;
                if (*p != '"')
                {
                    ADD_ERROR_MESSAGE("Unexpected trailing after the closing quote.");
                    result += p; //< Append the unexpected trailing to the result as is.
                    return result;
                }
                ++p; //< Skip the quote.
                break;
            case '\\':
            {
                ++p;
                result += decodeEscapeSequence(&p, errorMessage);
                break;
            }
            default:
                if (/*allow non-ASCII characters*/ (unsigned char) c < 128 && !isAsciiPrintable(c))
                    ADD_ERROR_MESSAGE("Found non-printable ASCII character " + toString(c) + ".");
                result += c;
                ++p;
                break;
        }
    }
}

template<int sizeOfChar> struct HexEscapeFmt {};
template<> struct HexEscapeFmt<1> { static constexpr const char* value = "\\x%02X"; };
template<> struct HexEscapeFmt<2> { static constexpr const char* value = "\\u%04X"; };
template<> struct HexEscapeFmt<4> { static constexpr const char* value = "\\U%08X"; };

template<typename Char>
static std::string hexEscapeFmtForChar()
{
    return HexEscapeFmt<sizeof(Char)>::value;
}

template<typename Char>
static std::string hexEscapeFmtForString()
{
    std::string value = HexEscapeFmt<sizeof(Char)>::value;

    // Empty quotes after `\x` are needed to limit the hex sequence if a digit follows it.
    if (value[0] == '\\' && value[1] == 'x')
        return value + "\"\"";

    return value;
}

template<typename Char>
using Unsigned = typename std::make_unsigned<Char>::type;

template<typename Char>
static std::string toStringFromChar(Char c)
{
    // NOTE: If the char is not a printable ASCII, we escape it via `\x` instead of specialized
    // escape sequences like `\r` because it looks more clear and universal.
    if (!isAsciiPrintable(c))
        return format("'" + hexEscapeFmtForChar<Char>() + "'", (Unsigned<Char>) c);
    if (c == '\'')
        return "'\\''";

    return std::string("'") + (char) c + "'";
}

template<typename Char>
static std::string escapeCharInString(Char c)
{
    switch (c)
    {
        case '\0': return "\\000"; // Three octal digits are needed to limit the octal sequence.
        case '\\': case '"': return std::string("\\") + (char) c;
        case '\n': return "\\n";
        case '\r': return "\\r";
        case '\t': return "\\t";
        // NOTE: Escape sequences `\a`, `\b`, `\f`, `\v` are not generated above: they are
        // rarely used, and their representation via `\x` is more clear.
        default:
            if (!isAsciiPrintable(c))
                return format(hexEscapeFmtForString<Char>(), (Unsigned<Char>) c);
            return std::string(1, (char) c);
    }
}

template<typename Char>
std::string toStringFromPtr(const Char* s)
{
    std::string result;
    if (s == nullptr)
    {
        result = "null";
    }
    else
    {
        result = "\"";
        for (const Char* p = s; *p != '\0'; ++p)
            result += escapeCharInString(*p);
        result += "\"";
    }
    return result;
}

std::string toString(const std::string& s)
{
    std::string result = "\"";
    for (char c: s)
        result += escapeCharInString(c);
    result += "\"";
    return result;
}

std::string toString(const std::wstring& w)
{
    std::string result = "\"";
    for (wchar_t c: w)
        result += escapeCharInString(c);
    result += "\"";
    return result;
}

std::string toString(bool b)
{
    return b ? "true" : "false";
}

std::string toString(const void* ptr)
{
    return ptr ? format("%p", ptr) : "null";
}

std::string toString(char c)
{
    return toStringFromChar(c);
}

std::string toString(const char* s)
{
    return toStringFromPtr(s);
}

std::string toString(wchar_t w)
{
    return toStringFromChar(w);
}

std::string toString(const wchar_t* w)
{
    return toStringFromPtr(w);
}

std::string vformat(const std::string& fmt, va_list args)
{
    const int char_count = vsnprintf(nullptr, 0, fmt.c_str(), args);
    if (char_count < 0)
        return "ERROR_FORMATTING(" + toString(fmt) + ")";
    std::string result;
    result.resize(char_count);
    vsnprintf(&result[0], char_count + /* terminating '\0'*/1, fmt.c_str(), args);
    return result;
}

bool fromString(const std::string& s, int* outValue)
{
    if (!outValue || s.empty())
        return false;

    // NOTE: std::stoi() is missing on Android, thus, using std::strtol().
    char* pEnd = nullptr;
    errno = 0; //< Required before std::strtol().
    const long v = std::strtol(s.c_str(), &pEnd, /*base*/ 0);

    if (v > INT_MAX || v < INT_MIN || errno != 0 || *pEnd != '\0')
        return false;

    *outValue = (int) v;
    return true;
}

bool fromString(const std::string& s, uint32_t* outValue)
{
    if (!outValue || s.empty())
        return false;

    char* pEnd = nullptr;
    errno = 0; //< Required before std::strtoll().
    // Parse to a 64-bit value to be able to prohibit negative values - strtoul() would allow them.
    const int64_t v = std::strtoll(s.c_str(), &pEnd, /*base*/ 0);

    if (v > UINT32_MAX || v < 0 || errno != 0 || *pEnd != '\0')
        return false;

    *outValue = (uint32_t) v;
    return true;
}

bool fromString(const std::string& s, uint64_t* outValue)
{
    if (!outValue || s.empty())
        return false;

    char* pEnd = nullptr;

    // Check for a negative value - strtoull() silently allows negative values.
    if (strtol(s.c_str(), &pEnd, /*base*/ 0) < 0)
        return false;

    errno = 0; //< Required before std::strtoull().
    const unsigned long long v = std::strtoull(s.c_str(), &pEnd, /*base*/ 0);

    if (errno != 0 || *pEnd != '\0')
        return false;

    *outValue = (uint64_t) v;
    return true;
}

bool fromString(const std::string& s, double* outValue)
{
    if (!outValue || s.empty())
        return false;

    char* pEnd = nullptr;
    errno = 0; //< Required before std::strtod().
    const double v = std::strtod(s.c_str(), &pEnd);

    if (errno != 0 || *pEnd != '\0')
        return false;

    *outValue = v;
    return true;
}

bool fromString(const std::string& s, float* outValue)
{
    if (!outValue || s.empty())
        return false;

    char* pEnd = nullptr;
    errno = 0; //< Required before std::strtod().
    const float v = std::strtof(s.c_str(), &pEnd);

    if (errno != 0 || *pEnd != '\0')
        return false;

    *outValue = v;
    return true;
}

bool fromString(const std::string& s, bool* value)
{
    if (s == "true" || s == "True" || s == "TRUE" || s == "1")
        *value = true;
    else if (s == "false" || s == "False" || s == "FALSE" || s == "0")
        *value = false;
    else
        return false;
    return true;
}

void stringReplaceAllChars(std::string* s, char sample, char replacement)
{
    for (int i = 0; i < (int) s->size(); ++i)
    {
        if ((*s)[i] == sample)
            (*s)[i] = replacement;
    }
}

void stringInsertAfterEach(std::string* s, char sample, const char* const insertion)
{
    for (int i = (int) s->size() - 1; i >= 0; --i)
    {
        if ((*s)[i] == sample)
            s->insert((size_t) (i + 1), insertion);
    }
}

void stringReplaceAll(std::string* s, const std::string& sample, const std::string& replacement)
{
    size_t pos = 0;
    while ((pos = s->find(sample, pos)) != std::string::npos)
    {
         s->replace(pos, sample.size(), replacement);
         pos += replacement.size();
    }
}

bool stringStartsWith(const std::string& s, const std::string& prefix)
{
    return s.rfind(prefix, /*pos*/ 0) == 0;
}

bool stringEndsWith(const std::string& s, const std::string& suffix)
{
    return s.size() >= suffix.size()
        && s.compare(s.size() - suffix.size(), suffix.size(), suffix) == 0;
}

bool stringContains(const std::string& s, const std::string& substring)
{
    return s.find(substring) != std::string::npos;
}

std::string trimString(const std::string& s)
{
    int start = 0;
    while (start < (int) s.size() && s.at(start) <= ' ')
        ++start;
    int end = (int) s.size() - 1;
    while (end >= 0 && s.at(end) <= ' ')
        --end;
    if (end < start)
        return "";
    return s.substr(start, end - start + 1);
}


std::string toUpper(const std::string& str)
{
    std::string result = str;
    for (auto& c: result)
        c = (c >= 'a' && c <= 'z') ? (c - 'a' + 'A') : c;
    return result;
}

#if defined(_WIN32)

std::string wideCharToStdString(const wchar_t* str)
{
    // Get the length of the resulting string.
    const int utf8Length = WideCharToMultiByte(
        CP_UTF8,
        /* Conversion flags */ 0,
        str,
        /* Autodetect string length */ -1,
        /* Pointer to converted string*/ nullptr,
        /* Size, in bytes, of the converted string. 0 to detect the needed size */ 0,
        /* Pointer to the default character */ nullptr,
        /* Pointer to a flag indicating if the default character was used */ nullptr);
    if (utf8Length == 0)
    {
        NX_KIT_ASSERT(GetLastError() == 0, "WideCharToMultiByte failed");
        return "";
    }

    std::string utf8String;
    utf8String.resize(utf8Length - 1);
    const int convertedLength = WideCharToMultiByte(
        CP_UTF8,
        /* Conversion flags */ 0,
        str,
        /* Autodetect string length */ -1,
        &utf8String[0],
        utf8Length,
        /* Pointer to the default character */ nullptr,
        /* Pointer to a flag indicating if the default character was used */ nullptr);
    NX_KIT_ASSERT(convertedLength == utf8Length);

    return utf8String;
}

#endif // defined(_WIN32)

} // namespace utils
} // namespace kit
} // namespace nx
