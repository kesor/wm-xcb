#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "sm-instance.h"
#include "sm-template.h"
#include "wm-log.h"

SMTemplate*
sm_template_create(
    const char*         name,
    const SMStateName*  states,
    uint32_t            num_states,
    const SMTransition* transitions,
    uint32_t            num_transitions,
    uint32_t            initial_state)
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
  tmpl->refs            = 1; /* the creator's reference */

  LOG_DEBUG("Created SMTemplate: %s", name);
  return tmpl;
}

SMTemplate*
sm_template_from_def(const SMTemplateDef* def)
{
  if (def == NULL) {
    LOG_ERROR("sm_template_from_def: no definition");
    return NULL;
  }

  SMTemplate* tmpl = sm_template_create(
      def->name, def->states, def->num_states,
      def->rows, def->num_rows, def->initial_state);
  if (tmpl == NULL)
    return NULL;

  char reason[160];
  if (!sm_template_validate(tmpl, reason, sizeof(reason))) {
    LOG_ERROR("state machine template '%s' is not valid: %s",
              def->name != NULL ? def->name : "(unnamed)", reason);
    sm_template_destroy(tmpl);
    return NULL;
  }

  return tmpl;
}

static bool
template_has_state(const SMTemplate* tmpl, uint32_t state)
{
  for (uint32_t i = 0; i < tmpl->num_states; i++) {
    if (tmpl->states[i].value == state)
      return true;
  }
  return false;
}

/*
 * Mark every state reachable from the initial state by following declared rows.
 * A row whose ends are not declared is skipped here; the caller reports it as
 * an error of its own before this runs.
 */
static void
mark_reachable(const SMTemplate* tmpl, uint8_t* reachable)
{
  bool changed = true;
  while (changed) {
    changed = false;
    for (uint32_t i = 0; i < tmpl->num_states; i++) {
      if (!reachable[i])
        continue;

      for (uint32_t r = 0; r < tmpl->num_transitions; r++) {
        if (tmpl->transitions[r].from_state != tmpl->states[i].value)
          continue;

        for (uint32_t j = 0; j < tmpl->num_states; j++) {
          if (reachable[j] || tmpl->states[j].value != tmpl->transitions[r].to_state)
            continue;
          reachable[j] = 1;
          changed      = true;
        }
      }
    }
  }
}

bool
sm_template_validate(const SMTemplate* tmpl, char* reason, size_t reason_size)
{
  if (reason != NULL && reason_size > 0)
    reason[0] = '\0';

  if (tmpl == NULL) {
    if (reason != NULL && reason_size > 0)
      snprintf(reason, reason_size, "no template");
    return false;
  }

  if (tmpl->name == NULL || tmpl->name[0] == '\0') {
    if (reason != NULL && reason_size > 0)
      snprintf(reason, reason_size, "no name");
    return false;
  }

  if (tmpl->states == NULL || tmpl->num_states == 0) {
    if (reason != NULL && reason_size > 0)
      snprintf(reason, reason_size, "no states declared");
    return false;
  }

  bool initial_declared = false;
  for (uint32_t i = 0; i < tmpl->num_states; i++) {
    if (tmpl->states[i].value == tmpl->initial_state) {
      initial_declared = true;
      break;
    }
  }
  if (!initial_declared) {
    if (reason != NULL && reason_size > 0)
      snprintf(reason, reason_size, "initial state %u is not declared among the states",
               tmpl->initial_state);
    return false;
  }

  for (uint32_t r = 0; r < tmpl->num_transitions; r++) {
    const SMTransition* row = &tmpl->transitions[r];

    if (!template_has_state(tmpl, row->from_state)) {
      if (reason != NULL && reason_size > 0)
        snprintf(reason, reason_size, "transition %u leaves undeclared state %u",
                 r, row->from_state);
      return false;
    }
    if (!template_has_state(tmpl, row->to_state)) {
      if (reason != NULL && reason_size > 0)
        snprintf(reason, reason_size, "transition %u enters undeclared state %u",
                 r, row->to_state);
      return false;
    }
    if (row->emit_event == 0) {
      if (reason != NULL && reason_size > 0)
        snprintf(reason, reason_size, "transition %u (%u -> %u) emits no event",
                 r, row->from_state, row->to_state);
      return false;
    }
  }

  uint8_t* reachable = calloc(tmpl->num_states, sizeof(uint8_t));
  if (reachable == NULL) {
    if (reason != NULL && reason_size > 0)
      snprintf(reason, reason_size, "out of memory while checking reachability");
    return false;
  }

  for (uint32_t i = 0; i < tmpl->num_states; i++) {
    if (tmpl->states[i].value == tmpl->initial_state)
      reachable[i] = 1;
  }
  mark_reachable(tmpl, reachable);

  bool valid = true;
  for (uint32_t i = 0; i < tmpl->num_states && valid; i++) {
    if (reachable[i])
      continue;

    if (reason != NULL && reason_size > 0)
      snprintf(reason, reason_size, "state %u is unreachable from the initial state",
               tmpl->states[i].value);
    valid = false;
  }

  free(reachable);
  return valid;
}

void
sm_template_retain(SMTemplate* tmpl)
{
  if (tmpl == NULL)
    return;
  tmpl->refs++;
}

void
sm_template_destroy(SMTemplate* tmpl)
{
  if (tmpl == NULL || tmpl->refs == 0)
    return;

  tmpl->refs--;
  if (tmpl->refs > 0) {
    LOG_DEBUG("SMTemplate %s still used by %u machine(s)", tmpl->name, tmpl->refs);
    return;
  }

  LOG_DEBUG("Destroying SMTemplate: %s", tmpl->name);

  /* Arrays are owned by the caller, not the template */
  /* Only the template struct itself is freed */
  free(tmpl);
}

const SMTransition*
sm_template_find_transition(
    const SMTemplate* tmpl,
    uint32_t          from_state,
    uint32_t          to_state)
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
sm_template_get_name(const SMTemplate* tmpl)
{
  if (tmpl == NULL)
    return NULL;
  return tmpl->name;
}

const char*
sm_template_state_name(const SMTemplate* tmpl, uint32_t state)
{
  if (tmpl == NULL || tmpl->states == NULL)
    return NULL;

  for (uint32_t i = 0; i < tmpl->num_states; i++) {
    if (tmpl->states[i].value == state)
      return tmpl->states[i].name;
  }

  return NULL;
}
