extern "C" {
#include <components/arena/arena.h>
}

#include <catch2/catch.hpp>
#include <cstring>

TEST_CASE("new and delete", "[arena]") {
  alignas(32) static uint8_t buffer[1024];

  datadog_php_arena *arena = datadog_php_arena_new(sizeof buffer, buffer);
  REQUIRE(arena);
  datadog_php_arena_delete(arena);
}

TEST_CASE("generic alignment", "[arena]") {
  uintptr_t addr = 8u;

  CHECK(datadog_php_arena_align_diff(addr + 0, 4) == 0u);
  CHECK(datadog_php_arena_align_diff(addr + 1, 4) == 3u);
  CHECK(datadog_php_arena_align_diff(addr + 2, 4) == 2u);
  CHECK(datadog_php_arena_align_diff(addr + 3, 4) == 1u);
  CHECK(datadog_php_arena_align_diff(addr + 4, 4) == 0u);
  CHECK(datadog_php_arena_align_diff(addr + 5, 4) == 3u);
}

TEST_CASE("allocator self-alignment", "[arena]") {
  alignas(16) uint8_t bytes[24];

  /* Since buffer is aligned to an even byte boundary, by passing in a pointer
   * + 1 (and taking 1 off the size to match), we can test that the arena
   * object itself gets correctly aligned in the buffer, since there is no
   * guarantee that the buffer has suitable alignment.
   */
  datadog_php_arena *arena = datadog_php_arena_new(sizeof bytes - 1, &bytes[1]);
  REQUIRE(arena);

  // uses some implementation-specific knowledge
  REQUIRE(((uint8_t *)arena) == bytes + alignof(uintptr_t));

  datadog_php_arena_delete(arena);
}

TEST_CASE("capacity and reset", "[arena]") {
  // This requires implementation-specific knowledge of datadog_php_arena
  alignas(8) uint8_t bytes[20];

  datadog_php_arena *arena = datadog_php_arena_new(sizeof bytes, bytes);
  REQUIRE(arena);

  uint8_t *i = datadog_php_arena_alloc(arena, 4, 1);
  REQUIRE(i);

  // This allocation should fail; no more room.
  uint8_t *j = datadog_php_arena_alloc(arena, 1, 1);
  REQUIRE(!j);

  datadog_php_arena_reset(arena);

  // Since we have reset the arena, this should allocate the same address
  uint8_t *k = datadog_php_arena_alloc(arena, 4, 1);
  REQUIRE(i == k);

  datadog_php_arena_delete(arena);
}

TEST_CASE("allocation alignment", "[arena]") {
  // This requires implementation-specific knowledge of datadog_php_arena
  alignas(16) uint8_t bytes[24];

  datadog_php_arena *arena = datadog_php_arena_new(sizeof bytes, bytes);
  REQUIRE(arena);

  /* Intentionally use a size and align combo that will set up the next alloc
   * to start misaligned.
   */
  uint8_t *i = datadog_php_arena_alloc(arena, 1, 1);
  REQUIRE(i);

  uint8_t *j = datadog_php_arena_alloc(arena, 4, 4);
  REQUIRE(j);
  REQUIRE(i + 4 == j);

  // This allocation should fail; no more room.
  uint8_t *k = datadog_php_arena_alloc(arena, 1, 1);
  REQUIRE(!k);

  datadog_php_arena_delete(arena);
}

TEST_CASE("allocation alignment capacity", "[arena]") {
  // This requires implementation-specific knowledge of datadog_php_arena
  alignas(16) uint8_t bytes[24];

  datadog_php_arena *arena = datadog_php_arena_new(sizeof bytes, bytes);
  REQUIRE(arena);

  uint8_t *i = datadog_php_arena_alloc(arena, 1, 1);
  REQUIRE(i);

  // This allocation would fit except for its alignment
  uint8_t *j = datadog_php_arena_alloc(arena, 7, 2);
  REQUIRE(!j);

  datadog_php_arena_delete(arena);
}

TEST_CASE("string allocation", "[arena]") {
  alignas(16) uint8_t bytes[64];

  datadog_php_arena *arena = datadog_php_arena_new(sizeof bytes, bytes);
  REQUIRE(arena);

  /* A 7 + 1 length string was chosen on purpose to make the next allocation
   * easy to align for math used later on.
   */
  const char *datadog = "datadog";

  char *copy =
      datadog_php_arena_alloc_str(arena, strlen(datadog), datadog);
  CHECK(copy);

  // `copy` should have a unique address...
  REQUIRE((void*) copy != (void*)datadog);

  // ...but should be equal.
  REQUIRE(memcmp(copy, datadog, strlen(datadog)) == 0);

  /* Calculate how much space remains to make an allocation fail. The earlier
   * comment about alignment comes into play here too.
   */
  size_t remaining = sizeof bytes - (copy - (char*)bytes) - strlen(copy) - 1;

  // The buffer needs to be large enough to have remaining bytes.
  REQUIRE(remaining > 0);

  void *obj = malloc(remaining);
  REQUIRE(obj);
  memset(obj, 'd', remaining);

  // Since alloc_str will null-terminate, remaining + 1 will push it over.
  char *obj2 = datadog_php_arena_alloc_str(arena, remaining, (char*)obj);
  CHECK(!obj2);
  free(obj);

  /* Due to the empty string optimization, we should be able to allocate at
   * least remaining + 1 empty strings, though.
   */
  for (size_t i = 0; i != remaining + 1; ++i) {
    INFO("Loop iteration " << i)
    REQUIRE(datadog_php_arena_alloc_str(arena, 0, datadog));
  }

  datadog_php_arena_delete(arena);
}
