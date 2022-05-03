#ifndef DATADOG_PHP_ONCE_H
#define DATADOG_PHP_ONCE_H

#include <stdatomic.h>
#include <uv.h>

// Treat this as opaque!
struct datadog_php_once_s {
  atomic_bool initialized;
  uv_mutex_t mutex;
};

/**
 * Allocate once resources. NOT THREAD SAFE!
 * Returns zero on success. Use the uv error handling API to inspect the value:
 * http://docs.libuv.org/en/v1.x/errors.html#api
 */
int datadog_php_once_ctor(struct datadog_php_once_s *once);

/**
 * Destroy the once, freeing resources. NOT THREAD SAFE! The once may be
 * reused by calling datadog_php_once_ctor, but no other operations are valid
 * after this call.
 */
void datadog_php_once_dtor(struct datadog_php_once_s *once);

/**
 * Initialize the `once`. The `init_routine` will be called exactly once. Other
 * callers will return only after `init_routine` has been called.
 */
void datadog_php_once(struct datadog_php_once_s *once,
                      void (*init_routine)(void));

#endif // DATADOG_PHP_ONCE_H
