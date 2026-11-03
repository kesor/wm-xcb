#ifndef _SM_TRACE_H_
#define _SM_TRACE_H_

/*
 * State machine trace
 *
 * Each machine keeps its last SM_TRACE_DEPTH changes in a ring, so a dump can
 * say how the machine reached the state it is in. A refused request is kept
 * too: a keybinding that did nothing leaves no other record anywhere.
 *
 * The ring is a fixed array in the machine. It is written from the same place
 * that changes the state, so it costs one branch and no allocation on a path
 * that runs per event.
 */

#include <stdbool.h>
#include <stdint.h>

/* Forward declaration (defined in sm-instance.h) */
typedef struct StateMachine StateMachine;

#define SM_TRACE_DEPTH 8

/*
 * How the state changed. A raw write is reality: an X event said so, and the
 * machine accepted it. A transition is intent: a request that a guard may
 * refuse.
 */
typedef enum SMTraceVia {
  SM_VIA_RAW_WRITE = 0,
  SM_VIA_TRANSITION,
} SMTraceVia;

/*
 * What the guard of the transition row answered. SM_GUARD_NONE covers both a
 * row that declares no guard and a request that matched no row at all.
 */
typedef enum SMTraceGuard {
  SM_GUARD_NONE = 0,
  SM_GUARD_PASSED,
  SM_GUARD_REJECTED,
} SMTraceGuard;

typedef struct SMTraceEntry {
  uint32_t     from_state;
  uint32_t     to_state;   /* the requested state when the change was refused */
  uint32_t     emit_event; /* 0 when the machine emitted nothing */
  SMTraceVia   via;
  SMTraceGuard guard;
  bool         applied;    /* false: refused, the machine kept from_state */
} SMTraceEntry;

/*
 * Number of entries recorded, at most SM_TRACE_DEPTH. Saturates: the ring
 * drops the oldest entry rather than growing.
 */
uint32_t sm_trace_count(StateMachine* sm);

/*
 * Copy recorded entries into `out`, oldest first, at most `capacity` of them.
 * When more entries are recorded than the buffer holds, the newest ones are
 * copied. Returns the number copied.
 */
uint32_t sm_trace_snapshot(StateMachine* sm, SMTraceEntry* out, uint32_t capacity);

#endif /* _SM_TRACE_H_ */
