#ifndef _WM_HUB_H_
#define _WM_HUB_H_

#include <inttypes.h>
#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct HubComponent  HubComponent;
typedef struct HubTarget     HubTarget;
typedef struct Event         Event;
typedef struct HubTargetType HubTargetType;
typedef void (*EventHandler)(Event);

/* Type definitions */
typedef uint64_t TargetID;
typedef uint32_t RequestType;
typedef uint32_t EventType;

/*
 * Target Type
 *
 * Introduced by components, registered with the hub.
 * Provides both string-based lookup (extensible) and
 * integer-based lookup (fast).
 *
 * Components declare which target types they:
 * - INTRODUCE: Target types they own (must be registered before use)
 * - ACCEPT: Target types they can work with (looked up by name)
 */
struct HubTargetType {
  const char*   name;     /* e.g., "client", "monitor", "focused-client" */
  uint32_t      id;       /* unique integer ID, assigned on registration */
  HubComponent* owner;    /* component that introduced this type */
  bool          reserved; /* true once registered */
};

/*
 * Target type ID type alias for clarity
 */
typedef uint32_t TargetTypeId;

/* Invalid type ID sentinel - returned by hub_get_target_type_id_by_name() when not found */
#define TARGET_TYPE_INVALID ((TargetTypeId) UINT32_MAX)

/*
 * Request type constants
 * Used by components to handle requests via hub routing.
 */
enum {
  REQ_CLIENT_FULLSCREEN = 10,
};

/*
 * Backward compatibility: Legacy target type constants
 *
 * These are now deprecated. New code should use:
 *   hub_get_target_type_id_by_name("client")
 *   hub_get_target_type_id_by_name("monitor")
 *   etc.
 *
 * Default values match the registration order in hub_init().
 * After hub_init(), these values are equivalent to the dynamically
 * registered IDs due to the resolve_target_type_id() mapping.
 */
enum {
  TARGET_TYPE_CLIENT  = 0,
  TARGET_TYPE_MONITOR = 1,
  TARGET_TYPE_TAG     = 2,
  TARGET_TYPE_COUNT   = 3,
};

/* Legacy: TARGET_TYPE_NONE for terminating target arrays */
#define TARGET_TYPE_NONE UINT32_MAX

/* Invalid ID sentinel */
#define TARGET_ID_NONE   ((TargetID) 0)

/*
 * Request structure - passed to component executors
 *
 * Note: The request is passed by pointer. The executor must not store
 * a pointer to the request beyond the duration of the callback.
 * For async operations, copy any needed data before returning.
 */
struct HubRequest {
  RequestType type;
  TargetID    target;
  void*       data;
  uint64_t    correlation_id; /* for async response correlation */
};

/* Forward declaration for executor type */
typedef void (*RequestExecutor)(struct HubRequest* req);

/* Forward declarations for adoption hooks */
typedef void (*AdoptionHook)(struct HubTarget* target);

/*
 * Component structure
 *
 * Components are the primary extension point for the window manager.
 * They declare:
 * - INTRODUCED target types: Target types they own/introduce
 * - ACCEPTED target types: Target types they can work with
 * - Requests: Request types they handle
 * - Lifecycle hooks: init, shutdown, adopt, unadopt
 */
struct HubComponent {
  const char* name;

  /* Target types this component ACCEPTS (works with)
   * NULL-terminated array of target type names.
   * Resolved to HubTargetType* at component registration time.
   */
  const char** accepted_target_names;

  /* Resolved target types (populated at registration) */
  HubTargetType** accepted_targets;

  RequestType*    requests;   /* 0-terminated array of request types handled */
  RequestExecutor executor;   /* called when this component receives a request */
  AdoptionHook    on_adopt;   /* called when a target adopts this component */
  AdoptionHook    on_unadopt; /* called when a target unadopts this component */
  bool            registered;
};

/* Target structure */
struct HubTarget {
  TargetID     id;
  TargetTypeId type_id; /* ID for fast lookup, maps to HubTargetType */
  bool         registered;
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

/* Target type registration and lookup */
HubTargetType*  hub_register_target_type(const char* name, HubComponent* owner);
HubTargetType*  hub_get_target_type_by_name(const char* name);
uint32_t        hub_get_target_type_id_by_name(const char* name);
HubTargetType*  hub_get_target_type_by_id(uint32_t id);
HubTargetType** hub_get_all_target_types(uint32_t* count);

/* Target adoption */
HubComponent** hub_get_components_for_target_type(TargetTypeId type_id);
HubComponent** hub_get_components_for_target_type_name(const char* type_name);
void           hub_adopt_components_for_target(HubTarget* target);
void           hub_unadopt_components_for_target(HubTarget* target);

/* Target registration */
void        hub_register_target(HubTarget* target);
void        hub_unregister_target(TargetID id);
HubTarget*  hub_get_target_by_id(TargetID id);
HubTarget** hub_get_targets_by_type(TargetTypeId type_id);
HubTarget** hub_get_targets_by_type_name(const char* type_name);

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
uint32_t hub_target_type_count(void);

#ifdef WM_HUB_TESTING
/* Request routing (for testing) */
RequestExecutor hub_get_executor_for_request(RequestType type);
#endif

#endif