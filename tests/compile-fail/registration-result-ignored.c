/*
 * The hub says whether it took a component, and a caller that drops the answer
 * leaves the WM without that component and nothing on stderr. The declaration
 * carries warn_unused_result, so the dropped answer must not compile. Built
 * both ways like the other fixtures: with REFUSAL_CONTROL the call is checked
 * and the file compiles, without it the call stands alone and the compiler
 * refuses it.
 */
#include "src/core/wm-hub.h"

void
probe(HubComponent* comp)
{
#ifdef REFUSAL_CONTROL
  if (hub_register_component(comp) != 0) {
    return;
  }
#else
  hub_register_component(comp);
#endif
}
