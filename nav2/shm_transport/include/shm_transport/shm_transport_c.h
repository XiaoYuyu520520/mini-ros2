#ifndef SHM_TRANSPORT_SHM_TRANSPORT_C_H
#define SHM_TRANSPORT_SHM_TRANSPORT_C_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Every entry point carries the `shm_channel_` prefix. Bare names would have
// worked on Windows, but POSIX owns two of them: <sys/mman.h> declares
// shm_open() and shm_unlink(), so a C ABI named after them does not even
// compile there - and a shared library exporting its own `shm_open` would
// interpose over the real one for anything loaded after it.
typedef struct shm_channel_s * shm_channel_t;

// All functions return 0 on success, negative on error.
shm_channel_t shm_channel_create(const char * name, uint32_t slot_count, uint32_t slot_size);
shm_channel_t shm_channel_open(const char * name);

int shm_channel_publish(shm_channel_t ch, const void * data, size_t size);
int shm_channel_subscribe(
  shm_channel_t ch, void * out, size_t capacity, size_t * out_size, int timeout_ms);

uint64_t shm_channel_published_count(shm_channel_t ch);
uint64_t shm_channel_read_count(shm_channel_t ch);
uint64_t shm_channel_dropped_count(shm_channel_t ch);
void shm_channel_close(shm_channel_t ch);

#ifdef __cplusplus
}
#endif

#endif  /* SHM_TRANSPORT_SHM_TRANSPORT_C_H */
