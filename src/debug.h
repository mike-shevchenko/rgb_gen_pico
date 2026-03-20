#pragma once

#include <stdio.h>

#include <pico/stdlib.h>

#include "nx/kit/utils.h"

namespace debug {

namespace detail {

// Ssometimes on assertion failure a long message could not be delivered on the serial console, so
// we print this short message first: ASSERT <source_file_basename>:<line>
void PrintBriefAssertionFailureMessage(const char* const file_basename, int line);

// NOTE: The assertion failure handlers always return false to simplify the macros.

bool __printflike(/*fmt*/4, /*...*/5) AssertFailed(
    const char* condition_str, const char* file_basename, int line, const char* fmt, ...);

// NOTE: Force-inline is needed to avoid code segment bloating.
template <typename... Args>
bool __force_inline AssertFailedInlineWrapper(
    const char* condition_str, const char* file_basename, int line, Args&&... args) {
  PrintBriefAssertionFailureMessage(file_basename, line);
  if constexpr (sizeof...(args) > 0) { // fms it present.
    return AssertFailed(condition_str, file_basename, line, std::forward<Args>(args)...);
  } else {
    return AssertFailed(condition_str, file_basename, line, /*fmt=*/nullptr);
  }
}

bool __printflike(/*fmt*/8, /*...*/9) AssertCmpFailed(
    const char* lhs_str,
    const char* op_str,
    const char* rhs_str,
    const char* lhs_val,
    const char* rhs_val,
    const char* file_basename,
    int line,
    const char* fmt,
    ...);

// NOTE: The lambda for obtaining string representations of the LHS and RHS values is needed to
// delay the calls to toString() to the non-inline function, thus helping optimization.
//
// NOTE: Force-inline is needed to avoid code segment bloating.
template <typename GetLhsRhsValStringPairFunc, typename... Args>
bool __force_inline AssertCmpFailedInlineWrapper(
    const char* lhs_str,
    const char* op_str,
    const char* rhs_str,
    GetLhsRhsValStringPairFunc&& get_lhs_rhs_val_string_pair,
    const char* file_basename,
    int line,
    Args&&... args) {
  PrintBriefAssertionFailureMessage(file_basename, line);
  auto [lhs_val, rhs_val] = get_lhs_rhs_val_string_pair();
  if constexpr (sizeof...(args) > 0) { // fms it present.
    return AssertCmpFailed(lhs_str, op_str, rhs_str, lhs_val.c_str(), rhs_val.c_str(),
        file_basename, line, std::forward<Args>(args)...);
  } else {
    return AssertCmpFailed(lhs_str, op_str, rhs_str, lhs_val.c_str(), rhs_val.c_str(),
        file_basename, line, /*fmt=*/nullptr);
  }
}

}  // namespace detail

//-------------------------------------------------------------------------------------------------

void SetBuiltInLed(bool is_on);

// If the condition is false, log the failure with printf(), and if -DFAILURE=panic,
// call panic().
//
// ATTENTION: Unlike std library assert(), the condition is checked even in the Release build to
// log the failure.
//
// NOTE: The MESSAGE expression is calculated regardless of the condition.
//
// Additionally, you can handle the failure (because the application must keep going):
// ```
//     if (!ASSERT(objectPointer))
//         return false;
// ```
//
// Return the condition evaluation result.
#define ASSERT(CONDITION, /* optional message format and args */ ...) ( \
  [result = (CONDITION) || ::debug::detail::AssertFailedInlineWrapper( \
      #CONDITION, __FILE_NAME__, __LINE__ __VA_OPT__(,) __VA_ARGS__)]() { \
    return result; \
  }() \
)

// Similar to ASSERT(), but prints the values being compared.
#define ASSERT_CMP(LHS, OP, RHS, /* optional message format and args */ ...) ( \
  [result = ((LHS) OP (RHS)) \
      || ::debug::detail::AssertCmpFailedInlineWrapper( \
        #LHS, #OP, #RHS, \
        [&]() -> std::pair<std::string, std::string> { \
          return {nx::kit::utils::toString(LHS), nx::kit::utils::toString(RHS)}; \
        }, \
        __FILE_NAME__, __LINE__ __VA_OPT__(,) __VA_ARGS__)]() { \
    return result; \
  }() \
)

}  // namespace debug
