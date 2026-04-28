#ifndef _WM_HUB_H_
#define _WM_HUB_H_

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct HubComponent HubComponent;
typedef struct HubTarget HubTarget;
typedef struct Event Event;
typedef void (*EventHandler)(Event);

/* Type definitions */
typedef uint64_t TargetID;
typedef uint32_t TargetType;
typedef uint32_t RequestType;
typedef uint32_t EventType;

/* Target types */
enum {
	TARGET_TYPE_CLIENT,
	TARGET_TYPE_MONITOR,
	TARGET_TYPE_KEYBOARD,
	TARGET_TYPE_TAG,
	TARGET_TYPE_SYSTEM,
	TARGET_TYPE_COUNT,
};

/* Explicit sentinel for NULL-terminated target arrays (avoids collision with 0) */
#define TARGET_TYPE_NONE ((TargetType) UINT32_MAX)

/* Invalid ID sentinel */
#define TARGET_ID_NONE ((TargetID)0)

/* Component structure */
struct HubComponent {
	const char*    name;
	RequestType*   requests;   /* NULL-terminated array of request types handled */
	TargetType*    targets;    /* TARGET_TYPE_NONE-terminated array of accepted types */
	bool           registered;
};

/* Target structure */
struct HubTarget {
	TargetID       id;
	TargetType     type;
	bool           registered;
};

/*
 * Event structure passed to handlers
 *
 * Note: Event is passed by value. The handler must not store
 * a pointer to the event beyond the duration of the callback.
 */
struct Event {
  EventType type;
  TargetID  target;
  void*     data;
  void*     userdata;  /* per-subscription userdata */
};

/*
 * Subscriber structure
 */
struct Subscriber {
  EventHandler handler;
  void*        userdata;
};

/* Hub initialization */
void hub_init(void);
void hub_shutdown(void);

/* Component registration */
void             hub_register_component(HubComponent* comp);
void             hub_unregister_component(const char* name);
HubComponent*    hub_get_component_by_name(const char* name);
HubComponent*    hub_get_component_by_request_type(RequestType type);

/* Target registration */
void         hub_register_target(HubTarget* target);
void         hub_unregister_target(TargetID id);
HubTarget*   hub_get_target_by_id(TargetID id);
HubTarget**   hub_get_targets_by_type(TargetType type);

/* Event bus operations */

/*
 * Emit an event to all subscribers.
 * Handlers are called with the event (passed by value).
 * The handler may not store a pointer to the event after the call returns.
 */
void hub_emit(EventType type, TargetID target, void* data);

/*
 * Subscribe to an event type.
 * When the event is emitted, handler will be called with
 * the event and userdata (stored per-subscription).
 */
void hub_subscribe(EventType type, EventHandler handler, void* userdata);

/*
 * Unsubscribe a handler from an event type.
 * If handler is not subscribed, this is a no-op.
 */
void hub_unsubscribe(EventType type, EventHandler handler);

/* Utility */
uint32_t hub_component_count(void);
uint32_t hub_target_count(void);

#endif