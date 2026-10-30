#ifndef RCUTILS__LOGGING_H_
#define RCUTILS__LOGGING_H_

#include <stdarg.h>
#include <stdio.h>

#include "rcutils/types.h"

typedef enum RCUTILS_LOG_SEVERITY
{
  RCUTILS_LOG_SEVERITY_UNSET = 0,
  RCUTILS_LOG_SEVERITY_DEBUG = 10,
  RCUTILS_LOG_SEVERITY_INFO = 20,
  RCUTILS_LOG_SEVERITY_WARN = 30,
  RCUTILS_LOG_SEVERITY_ERROR = 40,
  RCUTILS_LOG_SEVERITY_FATAL = 50,
} RCUTILS_LOG_SEVERITY;

/* The shim has no level filtering: RCLCPP_* macros write through rclcpp's
 * logger directly, so accepting the request is the whole contract. */
static inline rcutils_ret_t rcutils_logging_set_logger_level(
  const char * logger_name, int level)
{
  (void)logger_name;
  (void)level;
  return RCUTILS_RET_OK;
}

#define RCUTILS_LOG_DEBUG(...) do {} while (0)
#define RCUTILS_LOG_INFO(...) do {} while (0)
#define RCUTILS_LOG_WARN(...) do {} while (0)
#define RCUTILS_LOG_ERROR(...) do {} while (0)

#endif  /* RCUTILS__LOGGING_H_ */
