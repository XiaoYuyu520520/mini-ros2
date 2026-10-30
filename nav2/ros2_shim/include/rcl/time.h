#ifndef RCL__TIME_H_
#define RCL__TIME_H_

#include <stdint.h>

typedef struct rcl_time_point_value_t
{
  int64_t nanoseconds;
} rcl_time_point_value_t;

typedef struct rcl_duration_value_t
{
  int64_t nanoseconds;
} rcl_duration_value_t;

#endif  /* RCL__TIME_H_ */
