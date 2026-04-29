#include <stdlib.h>

#include "sm-instance.h"
#include "sm-template.h"
#include "sm-registry.h"
#include "wm-hub.h"
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
sm_create(void* owner, SMTemplate* template, EventEmitter emit, void* emit_userdata)
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
  sm->emit          = emit;
  sm->emit_userdata = emit_userdata;

  /* Allocate hook lists for each phase */
  for (int i = 0; i < SM_HOOK_MAX; i++) {
    sm->hooks[i] = sm_hook_list_create();
    if (sm->hooks[i] == NULL) {
      LOG_ERROR("Failed to allocate hook list for phase %d", i);
      /* Clean up any already-allocated hook lists */
      for (int j = 0; j < i; j++) {
        sm_hook_list_destroy(sm->hooks[j]);
        sm->hooks[j] = NULL;
      }
      free(sm);
      return NULL;
    }
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

/*
 * Emit a transition event.
 * This is an internal helper called by sm_raw_write and sm_transition
 * to emit events through the registered emitter.
 *
 * Note: hub_emit is declared in wm-hub.h and defined in wm-hub.c.
 * When linked with wm-hub.o, this function is available.
 */
static void
sm_emit_event(StateMachine* sm, uint32_t from_state, uint32_t to_state, uint32_t emit_event)
{
  if (emit_event == 0)
    return;


  if (sm->emit != NULL) {
    sm->emit(sm, from_state, to_state, sm->emit_userdata);
  } else {
    /*
     * Fallback: use hub_emit from wm-hub.c.
     *
     * StateMachine.owner is a generic owner pointer, so it must not be
     * cast to HubTarget* here. If a caller needs target-specific event
     * emission, it should provide sm->emit and sm->emit_userdata.
     */
    hub_emit(emit_event, TARGET_ID_NONE, sm->data);
  }
}

void
sm_raw_write(StateMachine* sm, uint32_t new_state)
{
  if (sm == NULL)
    return;

  uint32_t old_state = sm->current_state;
  LOG_DEBUG("sm_raw_write: %s: %u -> %u",
            sm->name, old_state, new_state);

  /* No guard check - reality is authoritative */
  sm->current_state = new_state;

  /* Emit transition event if a valid transition exists */
  SMTransition* t = sm_template_find_transition(
      sm->template, old_state, new_state);
  sm_emit_event(sm, old_state, new_state, t ? t->emit_event : 0);
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

  /* Run guards - use registry to find and call guard function */
  if (t->guard_fn != NULL) {
    LOG_DEBUG("sm_transition: checking guard '%s' for %s", t->guard_fn, sm->name);
    if (!sm_run_guard(sm, t->guard_fn, sm->data)) {
      LOG_DEBUG("sm_transition: guard '%s' rejected transition %s: %u -> %u",
                t->guard_fn, sm->name, sm->current_state, target_state);
      return false;
    }
    LOG_DEBUG("sm_transition: guard '%s' passed for %s", t->guard_fn, sm->name);
  } else {
    LOG_DEBUG("sm_transition: no guard for %s", sm->name);
  }

  /* Run post-guard hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_POST_GUARD], sm);

  /* Run pre-action hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_PRE_ACTION], sm);

  /* Execute action - use registry to find and call action function */
  if (t->action_fn != NULL) {
    if (!sm_run_action(sm, t->action_fn, sm->data)) {
      LOG_ERROR("sm_transition: action '%s' failed for %s", t->action_fn, sm->name);
      return false;
    }
    LOG_DEBUG("sm_transition: executed action '%s' for %s", t->action_fn, sm->name);
  }

  /* Update state */
  uint32_t old_state = sm->current_state;
  sm->current_state  = target_state;

  /* Run post-action hooks */
  sm_hook_list_run(sm->hooks[SM_HOOK_POST_ACTION], sm);

  /* Emit event */
  if (t->emit_event != 0) {
    /* Run pre-emit hooks */
    sm_hook_list_run(sm->hooks[SM_HOOK_PRE_EMIT], sm);

    LOG_DEBUG("sm_transition: emitted event %u for %s: %u -> %u",
              t->emit_event, sm->name, old_state, target_state);

    sm_emit_event(sm, old_state, target_state, t->emit_event);
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

void
sm_set_emitter(StateMachine* sm, EventEmitter emit, void* emit_userdata)
{
  if (sm == NULL)
    return;
  sm->emit          = emit;
  sm->emit_userdata = emit_userdata;
}
