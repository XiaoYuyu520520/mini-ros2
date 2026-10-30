#ifndef RMW__TYPES_H_
#define RMW__TYPES_H_

#include <stdint.h>

typedef struct rmw_request_id_s
{
  int64_t sequence_number;
  uint8_t writer_guid[16];
} rmw_request_id_t;

typedef struct rmw_response_id_s
{
  int64_t sequence_number;
} rmw_response_id_t;

#endif  /* RMW__TYPES_H_ */
