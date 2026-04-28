#include "xcb-handler.h"

#include <stdlib.h>
#include <string.h>

#include "wm-hub.h"
#include "wm-log.h"

/*
 * Handler registry - array of linked lists indexed by event type
 * XCB event types are 0-127 (standard) or extended types for GE events
 */
#define MAX_EVENT_TYPES 256

/* Handler storage */
typedef struct handler_entry {
  XCBHandler            handler;
  struct handler_entry* next;
} handler_entry_t;

static handler_entry_t* handlers[MAX_EVENT_TYPES];
static uint32_t         handler_counts[MAX_EVENT_TYPES];
static uint32_t         total_handlers = 0;

/*
 * Initialize the handler registry.
 */
void
xcb_handler_init(void)
{
  memset(handlers, 0, sizeof(handlers));
  memset(handler_counts, 0, sizeof(handler_counts));
  total_handlers = 0;
  LOG_DEBUG("XCB handler registry initialized");
}

/*
 * Shutdown the handler registry.
 */
void
xcb_handler_shutdown(void)
{
  /* Free all handler entries */
  for (int i = 0; i < MAX_EVENT_TYPES; i++) {
    handler_entry_t* entry = handlers[i];
    while (entry != NULL) {
      handler_entry_t* next = entry->next;
      free(entry);
      entry = next;
    }
    handlers[i] = NULL;
  }

  memset(handler_counts, 0, sizeof(handler_counts));
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

  /* Allocate and initialize handler entry */
  handler_entry_t* entry = malloc(sizeof(handler_entry_t));
  if (entry == NULL) {
    LOG_ERROR("Failed to allocate handler entry for event type %u", event_type);
    return -1;
  }
  memset(entry, 0, sizeof(handler_entry_t));
  entry->handler.event_type = event_type;
  entry->handler.component  = component;
  entry->handler.handler    = handler;
  entry->handler.next       = NULL;

  /* Add to linked list for this event type */
  if (handlers[event_type] == NULL) {
    handlers[event_type] = entry;
  } else {
    /* Find end of list and append */
    handler_entry_t* curr = handlers[event_type];
    while (curr->next != NULL) {
      curr = curr->next;
    }
    curr->next = entry;
    /* Also link handler chain for xcb_handler_next() iteration */
    curr->handler.next = &entry->handler;
  }

  handler_counts[event_type]++;
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

  if (handlers[event_type] == NULL) {
    return NULL;
  }

  return &handlers[event_type]->handler;
}

/*
 * Get next handler in chain.
 */
XCBHandler*
xcb_handler_next(XCBHandler* handler)
{
  if (handler == NULL) {
    return NULL;
  }

  return handler->next;
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
 */
void
xcb_handler_unregister_component(HubComponent* component)
{
  if (component == NULL) {
    return;
  }

  int removed = 0;

  /* Iterate through all event types */
  for (int i = 0; i < MAX_EVENT_TYPES; i++) {
    handler_entry_t** prev = &handlers[i];
    handler_entry_t*  curr = handlers[i];

    while (curr != NULL) {
      if (curr->handler.component == component) {
        /* Remove this entry */
        handler_entry_t* next = curr->next;
        *prev                 = next;
        free(curr);
        curr = next;
        handler_counts[i]--;
        total_handlers--;
        removed++;
        /* Note: prev is still valid as it points to the slot we just updated */
      } else {
        prev = &curr->next;
        curr = curr->next;
      }
    }

    /* Rebuild handler chain for remaining entries */
    handler_entry_t* chain_curr = handlers[i];
    while (chain_curr != NULL && chain_curr->next != NULL) {
      chain_curr->handler.next = &chain_curr->next->handler;
      chain_curr               = chain_curr->next;
    }
    if (chain_curr != NULL) {
      chain_curr->handler.next = NULL;
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

  return handler_counts[event_type];
}