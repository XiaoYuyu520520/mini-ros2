#ifndef KARTO_COMPAT__UINT_HPP_
#define KARTO_COMPAT__UINT_HPP_

// Karto's Mapper.cpp writes `for (uint i = 0; ...)` in a handful of places. The
// BSD `uint` typedef comes in through <sys/types.h> on Linux but the UCRT does
// not define it, so the translation units get it from here instead of patching
// upstream. Force-included via target_compile_options in CMakeLists.txt.

using uint = unsigned int;
static_assert(sizeof(uint) == sizeof(unsigned int), "Karto assumes `uint` is 32-bit");

#endif  // KARTO_COMPAT__UINT_HPP_
