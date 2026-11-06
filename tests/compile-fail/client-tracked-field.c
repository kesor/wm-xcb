/*
 * The client values a component must not write are behind accessors, and this
 * file is built both ways: with PRIVATE_STATE_CONTROL the accessor compiles,
 * without it the assignment to the field must not. scripts/check-private-state.sh
 * runs both, so a broken include path cannot pass for a refusal.
 */
#include "src/target/client.h"

void
probe(Client* c, bool shown)
{
#ifdef PRIVATE_STATE_CONTROL
  client_set_mapped(c, shown);
#else
  c->mapped = shown;
#endif
}
