#ifndef _WM_HUB_H_
#define _WM_HUB_H_

#include <stdbool.h>
#include <stdint.h>

/* Forward declarations */
typedef struct HubComponent HubComponent;
typedef struct HubTarget HubTarget;

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
HubTarget**  hub_get_targets_by_type(TargetType type);

/* Utility */
uint32_t hub_component_count(void);
uint32_t hub_target_count(void);

#endif