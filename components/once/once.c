#include "once.h"

#include <stdatomic.h>
#include <stdbool.h>
#include <uv.h>

int datadog_php_once_ctor(struct datadog_php_once_s *once) {
  atomic_store(&once->initialized, false);
  return uv_mutex_init(&once->mutex);
}

void datadog_php_once_dtor(struct datadog_php_once_s *once) {
  atomic_store(&once->initialized, true);
  uv_mutex_destroy(&once->mutex);
}

void datadog_php_once(struct datadog_php_once_s *once,
                      void (*init_routine)(void)) {
  // If it's already initialized we can avoid the lock.
  if (atomic_load(&once->initialized)) {
    return;
  }
  uv_mutex_lock(&once->mutex);
  // The value cannot be modified until the init routine has completed.
  if (atomic_load(&once->initialized) == false) {
    init_routine();
    atomic_store(&once->initialized, true);
  }
  uv_mutex_unlock(&once->mutex);
}
