#ifndef _SM_INSTANCE_INTERNAL_H_
#define _SM_INSTANCE_INTERNAL_H_

/*
 * The definition of struct StateMachine, kept out of sm-instance.h on purpose.
 *
 * current_state is the value the single-writer rule is about. With the
 * definition in the public header, a component that assigns it compiles and the
 * WM and the X server then disagree, days later, with no trace to the write. A
 * translation unit that does not include this header cannot name the field, so
 * that mistake is a build error. Include it from src/sm/sm-instance.c and from
 * the tests that check the framework's own internals, and from nowhere else.
 */

#include <stdbool.h>
#include <stdint.h>

#include "sm-instance.h"
#include "sm-template.h"

typedef struct SMHookList SMHookList;

/*
 * StateMachine structure
 * Instance of a state machine template.
 */
struct StateMachine {
  const char*  name;               /* instance name (from template) */
  uint32_t     current_state;      /* current state value */
  void*        owner;              /* owner target (client, monitor, etc.) */
  SMTemplate*  template;           /* reference to the template */
  void*        data;               /* instance-specific data */
  SMDataFree   data_free;          /* frees `data` on destroy; NULL when borrowed */
  SMHookList*  hooks[SM_HOOK_MAX]; /* hook lists for each phase */
  EventEmitter emit;               /* event emitter function (e.g., hub_emit) */
  void*        emit_userdata;      /* userdata passed to emit function */
};

#endif                             /* _SM_INSTANCE_INTERNAL_H_ */
