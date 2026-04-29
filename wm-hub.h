#ifndef _WM_HUB_H_
#define _WM_HUB_H_

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct HubComponent HubComponent;
typedef struct HubTarget    HubTarget;
typedef struct Event        Event;
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
#define TARGET_ID_NONE   ((TargetID) 0)

/* Request structure - passed to component executors */
struct HubRequest {
  RequestType type;
  TargetID    target;
  void*       data;
  uint64_t    correlation_id; /* for async response correlation */
};

/* Forward declaration for executor type */
typedef void (*RequestExecutor)(struct HubRequest* req);

/* Component structure */
struct HubComponent {
  const char*     name;
  RequestType*    requests; /* NULL-terminated array of request types handled */
  TargetType*     targets;  /* TARGET_TYPE_NONE-terminated array of accepted types */
  RequestExecutor executor; /* called when this component receives a request */
  bool            registered;
};

/* Target structure */
struct HubTarget {
  TargetID   id;
  TargetType type;
  bool       registered;
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
  void*     userdata; /* per-subscription userdata */
};

typedef void (*EventHandler)(struct Event);

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

/* Request routing */

/*
 * Send a request to the hub for routing to the appropriate component.
 * The hub will find the component that handles this request type
 * and call its executor with the request.
 *
 * @param type    The request type (e.g., REQ_CLIENT_FULLSCREEN)
 * @param target  The target ID to pass to the executor
 */
void hub_send_request(RequestType type, TargetID target);

/*
 * Send a request with data payload.
 * The data is passed to the component executor.
 *
 * @param type    The request type
 * @param target  The target ID
 * @param data    Arbitrary data to pass to the executor
 */
void hub_send_request_data(RequestType type, TargetID target, void* data);

/*
 * Send a request with correlation ID for async response tracking.
 *
 * @param type           The request type
 * @param target         The target ID
 * @param correlation_id Caller-provided ID to match with response events
 */
void hub_send_request_with_cid(RequestType type, TargetID target, uint64_t correlation_id);

/* Component registration */
void          hub_register_component(HubComponent* comp);
void          hub_unregister_component(const char* name);
HubComponent* hub_get_component_by_name(const char* name);
HubComponent* hub_get_component_by_request_type(RequestType type);

/* Target registration */
void        hub_register_target(HubTarget* target);
void        hub_unregister_target(TargetID id);
HubTarget*  hub_get_target_by_id(TargetID id);
HubTarget** hub_get_targets_by_type(TargetType type);

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

/* Request routing (for testing) */
RequestExecutor hub_get_executor_for_request(RequestType type);

#endif