/*
 * fuzz-rule.c - feed arbitrary strings to the window rules matcher.
 *
 * config_should_float() and config_get_tags() match a client's class, instance
 * and title against the compiled-in rules with a substring matcher. The strings
 * come from the client's WM_CLASS and WM_NAME properties: a hostile or buggy
 * client decides what they contain, and every rule in the table is tried against
 * them on the manage path.
 *
 * The harness also queries with strings that are not NUL-terminated at the end
 * of the buffer, because a property read that loses its terminator is exactly
 * the bug this is looking for.
 *
 * Build and run: make fuzz
 */

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#include <stdbool.h>

#include "src/config/config.def.h"
#include "src/config/config.h"

int
LLVMFuzzerTestOneInput(const uint8_t* data, size_t size)
{
  /* Three views of the same bytes: the whole input, a one-byte string, and a
     string that starts in the middle. */
  char* whole = malloc(size + 1);
  if (whole == NULL)
    return 0;
  memcpy(whole, data, size);
  whole[size] = '\0';

  (void) config_should_float(whole, whole, whole);
  (void) config_get_tags(whole, whole, whole);
  (void) config_should_float("", whole, "");
  (void) config_get_tags("", "", whole);
  if (size > 1) {
    (void) config_should_float(whole + 1, whole, whole + size / 2);
  }

  free(whole);
  return 0;
}
