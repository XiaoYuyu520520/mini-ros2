#ifndef RCL_ACTION__ACTION_SERVER_H_
#define RCL_ACTION__ACTION_SERVER_H_

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rcl_action_server_options_t
{
  int goal_service_qos;
  int result_service_qos;
  int cancel_service_qos;
  int feedback_topic_qos;
  int status_topic_qos;
} rcl_action_server_options_t;

static inline rcl_action_server_options_t rcl_action_server_get_default_options(void)
{
  rcl_action_server_options_t options = {0, 0, 0, 0, 0};
  return options;
}

#ifdef __cplusplus
}
#endif

#endif  /* RCL_ACTION__ACTION_SERVER_H_ */
