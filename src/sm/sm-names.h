/*
 * State machines more than one component reads
 *
 * A component may read a machine another component stored on a target, but it
 * may not include that component's header: docs/PHILOSOPHY.md section 2.1
 * makes the include a violation, and scripts/check-coupling.sh fails the build
 * on one. Reading means naming the machine and comparing its state, so the name
 * the target stores the machine under and the states a reader compares against
 * live here, where both components can include them.
 *
 * Only a machine with a reader outside the component that owns it belongs here.
 * A machine with one reader stays in its own component's header.
 */

#ifndef _SM_NAMES_H_
#define _SM_NAMES_H_

/*
 * The fullscreen machine. The client target stores it under the component's
 * name, so the name is the handle a reader looks up.
 */
#define FULLSCREEN_COMPONENT_NAME "fullscreen"

typedef enum FullscreenState {
  FULLSCREEN_STATE_WINDOWED   = 0,
  FULLSCREEN_STATE_FULLSCREEN = 1,
} FullscreenState;

/*
 * The floating machine, read by tiling to leave a floated client at the
 * rectangle it has.
 */
#define FLOATING_COMPONENT_NAME "floating"

typedef enum FloatingState {
  FLOATING_STATE_TILED    = 0,
  FLOATING_STATE_FLOATING = 1,
} FloatingState;

#endif /* _SM_NAMES_H_ */
