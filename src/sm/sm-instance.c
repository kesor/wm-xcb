#include <stdlib.h>

#include "sm-instance.h"
#include "sm-template.h"
#include "wm-log.h"

/*
 * Hook storage for each state machine instance
 */
typedef struct SMHook {
  SMHookFn       fn;
  void*          userdata;
  struct SMHook* next;
} SMHook;

struct SMHookList {
  SMHook* head;
};

static struct SMHookList*
sm_hook_list_create(void)
{
  struct SMHookList* list = malloc(sizeof(struct SMHookList));
  if (list == NULL)
    return NULL;
  list->head = NULL;
  return list;
}

static void
sm_hook_list_destroy(struct SMHookList* list)
{
  if (list == NULL)
    return;
  SMHook* curr = list->head;
  while (curr != NULL) {
    SMHook* next = curr->next;
    free(curr);
    curr = next;
  }
  free(list);
}

static void
sm_hook_list_add(struct SMHookList* list, SMHookFn fn, void* userdata)
{
  if (list == NULL || fn == NULL)
    return;
  SMHook* hook = malloc(sizeof(SMHook));
  if (hook == NULL)
    return;
  hook->fn       = fn;
  hook->userdata = userdata;
  hook->next     = list->head;
  list->head     = hook;
}

static void
sm_hook_list_remove(struct SMHookList* list, SMHookFn fn)
{
  if (list == NULL || fn == NULL)
    return;
  SMHook* curr = list->head;
  SMHook* prev = NULL;
  while (curr != NULL) {
    if (curr->fn == fn) {
      if (prev == NULL)
        list->head = curr->next;
      else
        prev->next = curr->next;
      free(curr);
      return;
    }
    prev = curr;
    curr = curr->next;
  }
}

static void
sm_hook_list_run(struct SMHookList* list, StateMachine* sm)
{
  if (list == NULL || sm == NULL)
    return;
  SMHook* curr = list->head;
  while (curr != NULL) {
    curr->fn(sm, curr->userdata);
    curr = curr->next;
  }
}

StateMachine*
sm_create(void* owner, SMTemplate* template)
{
  if (owner == NULL || template == NULL) {
    LOG_ERROR("sm_create: owner and template are required");
    return NULL;
  }

  StateMachine* sm = malloc(sizeof(StateMachine));
  if (sm == NULL) {
    LOG_ERROR("Failed to allocate StateMachine");
    return NULL;
  }

  sm->name          = template->name;
  sm->current_state = template->initial_state;
  sm->owner         = owner;
  sm->template      = template;
  sm->data          = NULL;

  /* Allocate hook lists for each phase */
  for (int i = 0; i < SM_HOOK_MAX; i++) {
    sm->hooks[i] = sm_hook_list_create();
  }

  /* Call template init function if present */
  if (template->init_fn != NULL) {
    template->init_fn(sm);
  }

  LOG_DEBUG("Created StateMachine: %s, initial_state=%u",
            sm->name, sm->current_state);
  return sm;
}

void
sm_destroy(StateMachine* sm)
{
  if (sm == NULL)
    return;

  LOG_DEBUG("Destroying StateMachine: %s", sm->name);

  /* Free hook lists */
  for (int i = 0; i < SM_HOOK_MAX; i++) {
    sm_hook_list_destroy(sm->hooks[i]);
    sm->hooks[i] = NULL;
  }

  free(sm);
}

uint32_t
sm_get_state(StateMachine* sm)
{
  if (sm == NULL)
    return 0;
  return sm->current_state;
}

bool
sm_can_transition(StateMachine* sm, uint32_t target_state)
{
  if (sm == NULL || sm->template == NULL)
    return false;

  SMTransition* t = sm_template_find_transition(
      sm->template, sm->current_state, target_state);

  return t != NULL;
}

uint32_t*
sm_get_available_transitions(StateMachine* sm, uint32_t* count)
{
  *count = 0;

  if (sm == NULL || sm->template == NULL || sm->template->transitions == NULL)
    return NULL;

  uint32_t  max_transitions = sm->template->num_transitions;
  uint32_t* states          = malloc(sizeof(uint32_t) * max_transitions);
  if (states == NULL)
    return NULL;

  for (uint32_t i = 0; i < sm->template->num_transitions; i++) {
    if (sm->template->transitions[i].from_state == sm->current_state) {
      states[(*count)++] = sm->template->transitions[i].to_state;
    }
  }

  return states;
}

void
sm_raw_write(StateMachine* sm, uint32_t new_state)
{
  if (sm == NULL)
    return;

  LOG_DEBUG("sm_raw_write: %s: %u -> %u",
            sm->name, sm->current_state, new_state);

  sm->current_state = new_state;

  /* Emit transition event if available */
  if (sm->template != NULL && sm->template->transitions != NULL) {
    SMTransition* t = sm_template_find_transition(
        sm->template, sm->current_state, new_state);
    if (t != NULL && t->emit_event != 0) {
      /* Event emission would go through hub_emit() in full implementation */
      LOG_DEBUG("sm_raw_write: emitted event %u", t->emit_event);
    }
  }
}

bool
sm_transition(StateMachine* sm, uint32_t target_state)
{
  if (sm == NULL || sm->template == NULL)
    return false;

  /* Find transition from current state to target state */
  SMTransition* t = sm_template_find_transition(
      sm->template, sm->current_state, target_state);

  if (t == NULL) {
    LOG_DEBUG("sm_transition: no valid transition %s: %u -> %u",
              sm->name, sm->current_state, target_state);
    return false;
  }

  /* Run pre-guard hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_PRE_GUARD], sm);

  /* Run guards (placeholder - would call guard functions by name) */
  if (t->guard_fn != NULL) {
    /* In full implementation, would look up and call guard function */
    LOG_DEBUG("sm_transition: guard %s would run here", t->guard_fn);
  }

  /* Run post-guard hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_POST_GUARD], sm);

  /* Run pre-action hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_PRE_ACTION], sm);

  /* Execute action (placeholder - would call action function by name) */
  if (t->action_fn != NULL) {
    LOG_DEBUG("sm_transition: action %s would run here", t->action_fn);
  }

  /* Update state */
  uint32_t old_state = sm->current_state;
  sm->current_state  = target_state;

  /* Run post-action hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_POST_ACTION], sm);

  /* Emit event */
  if (t->emit_event != 0) {
    LOG_DEBUG("sm_transition: emitted event %u for %s: %u -> %u",
              t->emit_event, sm->name, old_state, target_state);
    /* In full implementation, would call hub_emit() */
  }

  /* Run post-emit hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_POST_EMIT], sm);

  return true;
}

void
sm_add_hook(StateMachine* sm, SMHookPhase phase, SMHookFn fn, void* userdata)
{
  if (sm == NULL || phase >= SM_HOOK_MAX || fn == NULL)
    return;
  sm_hook_list_add(sm->hooks[phase], fn, userdata);
}

void
sm_remove_hook(StateMachine* sm, SMHookPhase phase, SMHookFn fn)
{
  if (sm == NULL || phase >= SM_HOOK_MAX || fn == NULL)
    return;
  sm_hook_list_remove(sm->hooks[phase], fn);
}

SMTemplate*
sm_get_template(StateMachine* sm)
{
  if (sm == NULL)
    return NULL;
  return sm->template;
}

const char*
sm_get_name(StateMachine* sm)
{
  if (sm == NULL)
    return NULL;
  return sm->name;
}