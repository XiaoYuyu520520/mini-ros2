#ifndef ROS2_SHIM__SYS__RESOURCE_H_
#define ROS2_SHIM__SYS__RESOURCE_H_

/* Windows has no <sys/resource.h>. ROS2 packages include it for `getrusage`
 * based CPU accounting -- slam_toolbox does exactly that, without ever calling
 * it. Providing the header keeps those sources unmodified. */

#if defined(_WIN32)

#include <sys/time.h>

#ifndef RUSAGE_SELF
#define RUSAGE_SELF 0
#endif
#ifndef RUSAGE_CHILDREN
#define RUSAGE_CHILDREN (-1)
#endif

struct rusage
{
  struct timeval ru_utime;
  struct timeval ru_stime;
  long ru_maxrss;
  long ru_ixrss;
  long ru_idrss;
  long ru_isrss;
  long ru_minflt;
  long ru_majflt;
  long ru_nswap;
  long ru_inblock;
  long ru_oublock;
  long ru_msgsnd;
  long ru_msgrcv;
  long ru_nsignals;
  long ru_nvcsw;
  long ru_nivcsw;
};

#ifndef RLIMIT_STACK
#define RLIMIT_STACK 3
#endif
#ifndef RLIMIT_DATA
#define RLIMIT_DATA 2
#endif

typedef unsigned long rlim_t;

struct rlimit
{
  rlim_t rlim_cur;
  rlim_t rlim_max;
};

#ifdef __cplusplus
extern "C" {
#endif

/* Windows sizes threads, not processes, so these are accepted and ignored.
 * slam_toolbox uses them only to widen the mapping thread's stack. */
static inline int getrlimit(int resource, struct rlimit * rlim)
{
  (void)resource;
  if (rlim != 0) {
    rlim->rlim_cur = 0;
    rlim->rlim_max = 0;
  }
  return 0;
}

static inline int setrlimit(int resource, const struct rlimit * rlim)
{
  (void)resource;
  (void)rlim;
  return 0;
}

static inline int getrusage(int who, struct rusage * usage)
{
  (void)who;
  if (usage != 0) {
    usage->ru_utime.tv_sec = 0;
    usage->ru_utime.tv_usec = 0;
    usage->ru_stime.tv_sec = 0;
    usage->ru_stime.tv_usec = 0;
    usage->ru_maxrss = 0;
    usage->ru_ixrss = 0;
    usage->ru_idrss = 0;
    usage->ru_isrss = 0;
    usage->ru_minflt = 0;
    usage->ru_majflt = 0;
    usage->ru_nswap = 0;
    usage->ru_inblock = 0;
    usage->ru_oublock = 0;
    usage->ru_msgsnd = 0;
    usage->ru_msgrcv = 0;
    usage->ru_nsignals = 0;
    usage->ru_nvcsw = 0;
    usage->ru_nivcsw = 0;
  }
  return 0;
}

#ifdef __cplusplus
}
#endif

#else
#include_next <sys/resource.h>
#endif

#endif /* ROS2_SHIM__SYS__RESOURCE_H_ */
