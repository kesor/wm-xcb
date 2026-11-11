/*
 * The client's tracked values live in a struct the header does not define, so
 * the pointer is to an incomplete type. Built both ways for the same reason as
 * client-tracked-field.c: the control build proves the header is readable.
 */
#include "src/target/client.h"

void
probe(Client* c, bool shown)
{
#ifdef REFUSAL_CONTROL
  client_set_mapped(c, shown);
#else
  c->tracked->mapped = shown;
#endif
}
