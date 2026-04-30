#include "xcb-handler.h"

#include <stdlib.h>
#include <string.h>

#include "wm-hub.h"
#include "wm-log.h"

/*
 * Handler registry - array of arrays indexed by event type
 * XCB event types are 0-127 (standard) or extended types for GE events
 */
#define MAX_EVENT_TYPES       256
#define MAX_HANDLERS_PER_TYPE 16

/* Handler storage per event type */
typedef struct {
  XCBHandler handlers[MAX_HANDLERS_PER_TYPE];
  int        count;
} handler_bucket_t;

static handler_bucket_t handlers[MAX_EVENT_TYPES];
static uint32_t         total_handlers = 0;

/*
 * Initialize the handler registry.
 */
void
xcb_handler_init(void)
{
  memset(handlers, 0, sizeof(handlers));
  total_handlers = 0;
  LOG_DEBUG("XCB handler registry initialized");
}

/*
 * Shutdown the handler registry.
 */
void
xcb_handler_shutdown(void)
{
  memset(handlers, 0, sizeof(handlers));
  total_handlers = 0;
  LOG_DEBUG("XCB handler registry shutdown");
}

/*
 * Register a handler for an XCB event type.
 */
int
xcb_handler_register(XCBEventType event_type, HubComponent* component, void (*handler)(void*))
{
  if (handler == NULL) {
    LOG_ERROR("Cannot register NULL handler for event type %u", event_type);
    return -1;
  }

  if (component == NULL) {
    LOG_ERROR("Cannot register handler with NULL component for event type %u", event_type);
    return -1;
  }

  if ((unsigned int) event_type >= MAX_EVENT_TYPES) {
    LOG_ERROR("Event type %u exceeds maximum (%d)", event_type, MAX_EVENT_TYPES - 1);
    return -1;
  }

  handler_bucket_t* bucket = &handlers[event_type];
  if (bucket->count >= MAX_HANDLERS_PER_TYPE) {
    LOG_ERROR("Too many handlers for event type %u (max %d)",
              event_type, MAX_HANDLERS_PER_TYPE);
    return -1;
  }

  /* Add to array - no linked list management needed */
  XCBHandler* h = &bucket->handlers[bucket->count];
  h->event_type = event_type;
  h->component  = component;
  h->handler    = handler;
  h->next       = NULL;

  bucket->count++;
  total_handlers++;

  LOG_DEBUG("Registered handler for event type %u (component: %p)",
            event_type, (void*) component);

  return 0;
}

/*
 * Lookup handlers for an event type.
 */
XCBHandler*
xcb_handler_lookup(XCBEventType event_type)
{
  if ((unsigned int) event_type >= MAX_EVENT_TYPES) {
    return NULL;
  }

  if (handlers[event_type].count == 0) {
    return NULL;
  }

  return &handlers[event_type].handlers[0];
}

/*
 * Get next handler in chain.
 * Uses the event_type stored in the handler to directly access its bucket.
 */
XCBHandler*
xcb_handler_next(XCBHandler* handler)
{
  if (handler == NULL) {
    return NULL;
  }

  /* Direct bucket lookup using event_type stored in handler */
  XCBEventType event_type = handler->event_type;
  handler_bucket_t* bucket = &handlers[event_type];

  /* Find current position in bucket */
  for (int i = 0; i < bucket->count; i++) {
    if (&bucket->handlers[i] == handler && i + 1 < bucket->count) {
      return &bucket->handlers[i + 1];
    }
  }

  return NULL;
}

/*
 * Dispatch an event to all registered handlers.
 */
void
xcb_handler_dispatch(void* event)
{
  if (event == NULL) {
    return;
  }

  /*
   * Extract response_type from event and mask off the XCB synthetic-event flag.
   * The high bit (0x80) of response_type is set for synthetic events generated
   * by the X server (e.g., via SendEvent). Masking ensures handlers are
   * dispatched correctly for both real and synthetic events.
   */
  XCBEventType event_type = ((uint8_t*) event)[0] & (uint8_t) ~0x80;

  /* Look up first handler for this event type */
  XCBHandler* handler = xcb_handler_lookup(event_type);
  if (handler == NULL) {
    LOG_DEBUG("No handler registered for event type %u", event_type);
    return;
  }

  /* Call all handlers for this event type */
  int call_count = 0;
  while (handler != NULL) {
    if (handler->handler != NULL) {
      LOG_DEBUG("Dispatching event type %u to handler (component: %p)",
                event_type, (void*) handler->component);
      handler->handler(event);
      call_count++;
    }
    handler = xcb_handler_next(handler);
  }

  LOG_DEBUG("Dispatched event type %u to %d handler(s)", event_type, call_count);
}

/*
 * Unregister all handlers for a component.
 * Uses stable removal (shift tail) to maintain registration order.
 */
void
xcb_handler_unregister_component(HubComponent* component)
{
  if (component == NULL) {
    return;
  }

  int removed = 0;

  /* Iterate through all event type buckets */
  for (int i = 0; i < MAX_EVENT_TYPES; i++) {
    handler_bucket_t* bucket = &handlers[i];

    /* Scan bucket and remove matching handlers while preserving order */
    for (int j = 0; j < bucket->count; ) {
      if (bucket->handlers[j].component == component) {
        /* Stable removal: shift remaining handlers left */
        memmove(&bucket->handlers[j],
                &bucket->handlers[j + 1],
                (bucket->count - j - 1) * sizeof(XCBHandler));
        bucket->count--;
        total_handlers--;
        removed++;
        /* Don't increment j - check the shifted element */
      } else {
        j++;
      }
    }
  }

  if (removed > 0) {
    LOG_DEBUG("Unregistered %d handler(s) for component: %p", removed, (void*) component);
  }
}

/*
 * Get total handler count.
 */
uint32_t
xcb_handler_count(void)
{
  return total_handlers;
}

/*
 * Get handler count for specific event type.
 */
uint32_t
xcb_handler_count_for_type(XCBEventType event_type)
{
  if ((unsigned int) event_type >= MAX_EVENT_TYPES) {
    return 0;
  }

  return (uint32_t) handlers[event_type].count;
}