/*
 * Tag Manager Component
 *
 * Handles tag view state for monitors. This component:
 * - Registers REQ_TAG_VIEW and REQ_TAG_TOGGLE requests
 * - Manages the TagViewSM state machine for monitors
 * - Shows/hides clients based on tag membership
 * - Emits EVT_TAG_CHANGED on tag state changes
 *
 * Keybindings that trigger these requests:
 * - Mod+1-9: REQ_TAG_VIEW (show specific tag)
 * - Mod+Shift+1-9: REQ_TAG_CLIENT_TOGGLE (move client to tag)
 *
 * Acceptance criteria:
 * - [x] Mod+1-9 shows specific tag
 * - [x] Mod+Shift+1-9 moves client to tag
 * - [x] Clients on hidden tags are not visible
 * - [x] Event is emitted on tag change
 *
 * Note: This component owns the TagViewSM, not the clients themselves.
 * Client tag membership is tracked in Client->tags bitmask.
 */

#ifndef _TAG_MANAGER_H_
#define _TAG_MANAGER_H_

#include <stdbool.h>
#include <stdint.h>

#include "wm-hub.h"

/* Forward declarations */
typedef struct Monitor Monitor;
typedef struct Client Client;
typedef struct StateMachine StateMachine;

/*
 * Tag Manager component name
 */
#define TAG_MANAGER_COMPONENT_NAME "tag-manager"

/*
 * Tag Manager request types
 */
enum TagManagerRequestType {
  REQ_TAG_VIEW            = 7,  /* View specific tag (1-9) */
  REQ_TAG_TOGGLE          = 8,  /* Toggle tag visibility */
  REQ_TAG_CLIENT_TOGGLE    = 9,  /* Move current client to/from tag */
};

/*
 * Tag Manager event types
 */
enum TagManagerEventType {
  EVT_TAG_CHANGED         = 20, /* Emitted when tag view state changes */
};

/*
 * Tag Manager SM states
 */
enum TagViewState {
  TAG_VIEW_NONE           = 0,  /* No tags visible (empty) */
  TAG_VIEW_ONE            = 1,  /* Exactly one tag visible */
  TAG_VIEW_MULTIPLE       = 2,  /* Multiple tags visible (all-tags view) */
};

/*
 * Tag Manager SM template name
 */
#define TAG_MANAGER_SM_NAME "tag-view"

/*
 * Tag Manager component structure
 */
typedef struct TagManagerComponent {
  HubComponent base;
  bool initialized;
} TagManagerComponent;

/*
 * Get the tag manager component instance
 */
TagManagerComponent* tag_manager_component_get(void);

/*
 * Check if component is initialized
 */
bool tag_manager_component_is_initialized(void);

/*
 * Initialize the tag manager component.
 * Registers with hub and sets up request handlers.
 */
bool tag_manager_component_init(void);

/*
 * Shutdown the tag manager component.
 */
void tag_manager_component_shutdown(void);

/*
 * Get the TagViewSM for a monitor.
 * Creates the SM on first access (on-demand allocation).
 */
StateMachine* tag_manager_get_view_sm(Monitor* m);

/*
 * Check if a client is visible based on its tags and the monitor's tag view.
 * A client is visible if any of its tags overlap with the monitor's visible tags.
 */
bool tag_manager_is_client_visible(Monitor* m, Client* c);

/*
 * Update client visibility based on tag view.
 * This is called after tag view changes to update all clients.
 */
void tag_manager_update_visibility(Monitor* m);

/*
 * Tag Manager request executor
 * Handles REQ_TAG_VIEW and REQ_TAG_TOGGLE requests.
 */
void tag_manager_handle_view_request(RequestType type, TargetID target, void* data);
void tag_manager_handle_toggle_request(RequestType type, TargetID target, void* data);
void tag_manager_handle_client_tag_toggle(RequestType type, TargetID target, void* data);

/*
 * Tag Manager listener callback
 * Called when other components emit relevant events.
 */
void tag_manager_listener(Event e);

/*
 * Get current visible tag mask for a monitor.
 * Returns the tag mask from the TagViewSM, or TAG_ALL_TAGS if no SM exists.
 */
uint32_t tag_manager_get_visible_tags(Monitor* m);

/*
 * Set visible tag mask for a monitor.
 * Creates TagViewSM if needed.
 */
void tag_manager_set_visible_tags(Monitor* m, uint32_t tag_mask);

/*
 * Adoption hook for tag manager component.
 * Called when a monitor adopts this component.
 */
void tag_manager_on_adopt(HubTarget* target);

#endif /* _TAG_MANAGER_H_ */