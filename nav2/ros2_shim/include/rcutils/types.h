#ifndef RCUTILS__TYPES_H_
#define RCUTILS__TYPES_H_

/* The subset of rcutils' return-code vocabulary that the ported packages touch.
 * Upstream spreads these over rcutils/types/rcutils_ret.h; one header is enough
 * here and it keeps `#include "rcutils/types.h"` working. */

typedef enum rcutils_ret_s
{
  RCUTILS_RET_OK = 0,
  RCUTILS_RET_WARN = 1,
  RCUTILS_RET_ERROR = 2,
  RCUTILS_RET_BAD_ALLOC = 10,
  RCUTILS_RET_INVALID_ARGUMENT = 11,
  RCUTILS_RET_NOT_ENOUGH_MEMORY = 12,
  RCUTILS_RET_NOT_INITIALIZED = 13,
  RCUTILS_RET_NOT_FOUND = 14,
} rcutils_ret_t;

#endif  /* RCUTILS__TYPES_H_ */
