#pragma once

#include <stdio.h>

#include "nx/kit/utils.h"

namespace debug {

namespace detail {

// NOTE: The assertion failure handlers always return false to simplify the macros.

bool __printflike(/*fmt*/4, /*...*/5) AssertFailed(
    const char* condition_str, const char* file, int line, const char* fmt, ...);

inline bool AssertFailed(const char* condition_str, const char* file, int line) {
  return AssertFailed(condition_str, file, line, /*fmt=*/nullptr);
}

bool __printflike(/*fmt*/8, /*...*/9) AssertCmpFailed(
  const char* lhs_str,
  const char* op_str,
  const char* rhs_str,
  const char* lhs_val,
  const char* rhs_val,
  const char* file,
  int line,
  const char* fmt,
  ...);

inline bool AssertCmpFailed(const char* lhs_str, const char* op_str,
    const char* rhs_str, const char* lhs_val, const char* rhs_val, const char* file, int line) {
  return AssertCmpFailed(
      lhs_str, op_str, rhs_str, lhs_val, rhs_val, file, line, /*fmt=*/nullptr);
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
  [result = (CONDITION) || ::debug::detail::AssertFailed( \
      #CONDITION, __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)]() { \
    return result; \
  }() \
)

#define ASSERT_CMP(LHS, OP, RHS, /* optional message format and args */ ...) ( \
  [result = ((LHS) OP (RHS)) \
      || ::debug::detail::AssertCmpFailed( \
        #LHS, #OP, #RHS, \
        nx::kit::utils::toString(LHS).c_str(), \
        nx::kit::utils::toString(RHS).c_str(), \
        __FILE__, __LINE__ __VA_OPT__(,) __VA_ARGS__)]() { \
    return result; \
  }() \
)

}  // namespace debug
