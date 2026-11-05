/*
 * Hub event type tests
 *
 * The event bus indexes its subscriber table by the event value, so a duplicate
 * delivers one component's events to another component's subscribers, and a
 * value above the table is dropped. src/event-types.h documents the range each
 * component owns; this list names every constant the tree can emit, so both
 * defects fail here instead of at the first event that goes missing.
 */

#include <stdbool.h>
#include <stddef.h>

#include "src/actions/launcher.h"
#include "src/components/client-list.h"
#include "src/components/connection-sm.h"
#include "src/components/floating.h"
#include "src/components/focus.h"
#include "src/components/fullscreen.h"
#include "src/components/tag-manager.h"
#include "src/components/tiling.h"
#include "src/event-types.h"
#include "test-registry.h"
#include "test-wm.h"
#include "wm-hub.h"

typedef struct KnownEventType {
  const char* name;
  EventType   type;
  EventType   range_start;
  EventType   range_end;
} KnownEventType;

#define KNOWN(type, start, end) { #type, (EventType) (type), (EventType) (start), (EventType) (end) }

static const KnownEventType known_events[] = {
  KNOWN(EVT_CLIENT_CREATED, EVT_CLIENT_LIST_BASE, EVT_CLIENT_LIST_LIMIT),
  KNOWN(EVT_CLIENT_DESTROYED, EVT_CLIENT_LIST_BASE, EVT_CLIENT_LIST_LIMIT),
  KNOWN(EVT_CLIENT_MANAGED, EVT_CLIENT_LIST_BASE, EVT_CLIENT_LIST_LIMIT),
  KNOWN(EVT_CLIENT_UNMANAGED, EVT_CLIENT_LIST_BASE, EVT_CLIENT_LIST_LIMIT),
  KNOWN(EVT_FULLSCREEN_ENTERED, EVT_FULLSCREEN_BASE, EVT_FULLSCREEN_LIMIT),
  KNOWN(EVT_FULLSCREEN_EXITED, EVT_FULLSCREEN_BASE, EVT_FULLSCREEN_LIMIT),
  KNOWN(EVT_FULLSCREEN_FAILED, EVT_FULLSCREEN_BASE, EVT_FULLSCREEN_LIMIT),
  KNOWN(EVT_TAG_CHANGED, EVT_TAG_MANAGER_BASE, EVT_TAG_MANAGER_LIMIT),
  KNOWN(EVT_CLIENT_UNFOCUSED, EVT_FOCUS_BASE, EVT_FOCUS_LIMIT),
  KNOWN(EVT_CLIENT_FOCUSED, EVT_FOCUS_BASE, EVT_FOCUS_LIMIT),
  KNOWN(EVT_LAYOUT_CHANGED, EVT_TILING_BASE, EVT_TILING_LIMIT),
  KNOWN(EVT_MONITOR_DISCONNECTED, EVT_CONNECTION_BASE, EVT_CONNECTION_LIMIT),
  KNOWN(EVT_MONITOR_CONNECTED, EVT_CONNECTION_BASE, EVT_CONNECTION_LIMIT),
  KNOWN(EVT_LAUNCHER_FAILED, EVT_LAUNCHER_BASE, EVT_LAUNCHER_LIMIT),
  KNOWN(EVT_CLIENT_FLOATED, EVT_FLOATING_BASE, EVT_FLOATING_LIMIT),
  KNOWN(EVT_CLIENT_TILED, EVT_FLOATING_BASE, EVT_FLOATING_LIMIT),
  KNOWN(EVT_FLOATING_FAILED, EVT_FLOATING_BASE, EVT_FLOATING_LIMIT),
};

#define KNOWN_EVENT_COUNT (sizeof(known_events) / sizeof(known_events[0]))

/*
 * Test: every event constant has its own value.
 */
void
test_event_types_are_unique(void)
{
  LOG_CLEAN("== Testing every hub event value is unique");

  bool seen[HUB_MAX_EVENT_TYPES] = { false };

  for (size_t i = 0; i < KNOWN_EVENT_COUNT; i++) {
    const KnownEventType* e = &known_events[i];

    if (e->type < HUB_MAX_EVENT_TYPES) {
      if (seen[e->type]) {
        LOG_CLEAN("  %s reuses event value %u", e->name, (unsigned) e->type);
      }
      assert(seen[e->type] == false);
      seen[e->type] = true;
    }
  }
}

/*
 * Test: no event value reaches past the event bus table.
 */
void
test_event_types_fit_the_bus(void)
{
  LOG_CLEAN("== Testing every hub event value fits the event bus");

  for (size_t i = 0; i < KNOWN_EVENT_COUNT; i++) {
    const KnownEventType* e = &known_events[i];

    if (e->type >= HUB_MAX_EVENT_TYPES) {
      LOG_CLEAN("  %s is %u, above the %d slots", e->name, (unsigned) e->type, HUB_MAX_EVENT_TYPES);
    }
    assert(e->type < HUB_MAX_EVENT_TYPES);
    assert(e->range_end <= HUB_MAX_EVENT_TYPES);
  }
}

/*
 * Test: every event stays inside the range its component owns.
 */
void
test_event_types_stay_in_their_component_range(void)
{
  LOG_CLEAN("== Testing every hub event stays in its component range");

  for (size_t i = 0; i < KNOWN_EVENT_COUNT; i++) {
    const KnownEventType* e = &known_events[i];

    if (e->type < e->range_start || e->type >= e->range_end) {
      LOG_CLEAN("  %s is %u, outside its range %u-%u",
                e->name, (unsigned) e->type, (unsigned) e->range_start, (unsigned) e->range_end - 1);
    }
    assert(e->type >= e->range_start);
    assert(e->type < e->range_end);
  }
}

TEST_GROUP(EventTypes, {
  test_event_types_are_unique();
  test_event_types_fit_the_bus();
  test_event_types_stay_in_their_component_range();
});
