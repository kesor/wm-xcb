#include <stdlib.h>
#include <string.h>

#include "sm-instance.h"
#include "sm-template.h"
#include "wm-log.h"

SMTemplate*
sm_template_create(
    const char*   name,
    uint32_t*     states,
    uint32_t      num_states,
    SMTransition* transitions,
    uint32_t      num_transitions,
    uint32_t      initial_state)
{
  SMTemplate* tmpl = malloc(sizeof(SMTemplate));
  if (tmpl == NULL) {
    LOG_ERROR("Failed to allocate SMTemplate");
    return NULL;
  }

  tmpl->name            = name;
  tmpl->states          = states;
  tmpl->num_states      = num_states;
  tmpl->transitions     = transitions;
  tmpl->num_transitions = num_transitions;
  tmpl->initial_state   = initial_state;

  LOG_DEBUG("Created SMTemplate: %s", name);
  return tmpl;
}

void
sm_template_destroy(SMTemplate* tmpl)
{
  if (tmpl == NULL)
    return;

  LOG_DEBUG("Destroying SMTemplate: %s", tmpl->name);

  /* Arrays are owned by the caller, not the template */
  /* Only the template struct itself is freed */
  free(tmpl);
}

SMTransition*
sm_template_find_transition(
    SMTemplate* tmpl,
    uint32_t    from_state,
    uint32_t    to_state)
{
  if (tmpl == NULL || tmpl->transitions == NULL)
    return NULL;

  for (uint32_t i = 0; i < tmpl->num_transitions; i++) {
    if (tmpl->transitions[i].from_state == from_state &&
        tmpl->transitions[i].to_state == to_state) {
      return &tmpl->transitions[i];
    }
  }

  return NULL;
}

const char*
sm_template_get_name(SMTemplate* tmpl)
{
  if (tmpl == NULL)
    return NULL;
  return tmpl->name;
}