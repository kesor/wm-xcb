#include <stdbool.h>
#include <string.h>
#include <xcb/xcb.h>

#include "src/config/config.def.h"
#include "src/core/wm-log.h"
#include "src/core/wm-states.h"
#include "src/core/wm-xcb.h"

void state_idle(struct Context* ctx);
void state_window_move(struct Context* ctx);
void state_window_resize(struct Context* ctx);

const char*
event_type_name(enum EventType event_type)
{
  switch (event_type) {
  case EventTypeNone:
    return "EventTypeNone";
  case EventTypeKeyPress:
    return "EventTypeKeyPress";
  case EventTypeKeyRelease:
    return "EventTypeKeyRelease";
  case EventTypeBtnPress:
    return "EventTypeBtnPress";
  case EventTypeBtnRelease:
    return "EventTypeBtnRelease";
  default:
    return "INVALID";
  }
}


#define MOD_KEY_NAME_BUF_SIZE 80

const char*
mod_key_name(enum ModKey mod_key)
{
  static char buf[MOD_KEY_NAME_BUF_SIZE];
  char*       dst       = buf;
  size_t      remaining = sizeof(buf);

  if (mod_key & KeyShift) {
    size_t n = strlcpy(dst, "KeyShift|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & KeyLock) {
    size_t n = strlcpy(dst, "KeyLock|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & KeyMod1) {
    size_t n = strlcpy(dst, "KeyMod1|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & KeyMod2) {
    size_t n = strlcpy(dst, "KeyMod2|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & KeyMod3) {
    size_t n = strlcpy(dst, "KeyMod3|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & KeyMod4) {
    size_t n = strlcpy(dst, "KeyMod4|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & KeyMod5) {
    size_t n = strlcpy(dst, "KeyMod5|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & Btn1) {
    size_t n = strlcpy(dst, "Btn1|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & Btn2) {
    size_t n = strlcpy(dst, "Btn2|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & Btn3) {
    size_t n = strlcpy(dst, "Btn3|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & Btn4) {
    size_t n = strlcpy(dst, "Btn4|", remaining);
    dst += n;
    remaining = (n < remaining) ? remaining - n : 0;
  }
  if (mod_key & Btn5) {
    size_t n = strlcpy(dst, "Btn5|", remaining);
    /* NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) */
    dst += n;
    /* NOLINTNEXTLINE(clang-analyzer-deadcode.DeadStores) */
    remaining = (n < remaining) ? remaining - n : 0;
  }

  /* remove the trailing '|' character */
  if (dst > buf) {
    /* NOLINTNEXTLINE(clang-analyzer-security.ArrayBound) */
    *(dst - 1) = '\0';
  } else {
    strlcpy(buf, "INVALID", sizeof(buf));
  }

  return buf;
}

struct Context ctx = {
  .state              = STATE_IDLE,
  .window_target_type = WndTypeNone,
  .event_type         = EventTypeNone,
  .key_mod            = KeyNone,
  .key                = XCB_NONE,
  .btn_mod            = KeyNone,
  .btn                = XCB_NONE,
  .root               = XCB_NONE,
  .root_x             = XCB_NONE,
  .root_y             = XCB_NONE,
  .child              = XCB_NONE,
  .event_x            = XCB_NONE,
  .event_y            = XCB_NONE,
  .state_funcs        = {
                         state_idle,
                         state_window_move,
                         state_window_resize,
                         }
};

void
set_state(struct Context* ctx, enum State new_state)
{
  ctx->state = new_state;
}

/*
 * The key fields of a press or release into the context the transition table
 * reads.
 */
static void
context_from_key(const xcb_key_press_event_t* key_event, enum EventType type)
{
  ctx.event_type = type;
  ctx.key_mod    = (enum ModKey) key_event->state;
  ctx.key        = key_event->detail;
  ctx.root       = key_event->root;
  ctx.root_x     = key_event->root_x;
  ctx.root_y     = key_event->root_y;
  ctx.child      = key_event->child;
  if (key_event->same_screen) {
    ctx.event_x = key_event->event_x;
    ctx.event_y = key_event->event_y;
  }
}

/* The same for a button press or release. */
static void
context_from_button(const xcb_button_press_event_t* btn_event, enum EventType type)
{
  ctx.event_type = type;
  ctx.btn_mod    = (enum ModKey) btn_event->state;
  ctx.btn        = btn_event->detail;
  ctx.root       = btn_event->root;
  ctx.root_x     = btn_event->root_x;
  ctx.root_y     = btn_event->root_y;
  ctx.child      = btn_event->child;
  if (btn_event->same_screen) {
    ctx.event_x = btn_event->event_x;
    ctx.event_y = btn_event->event_y;
  }
}

/*
 * Whether the row's chord is the key chord the event carried. A chord is a key
 * or a button, never both, so the event type decides which half is compared.
 */
static bool
key_chord_matches(const struct StateTransition* t)
{
  return (ctx.event_type == EventTypeKeyPress || ctx.event_type == EventTypeKeyRelease) &&
         t->mod == ctx.key_mod && t->key == ctx.key;
}

static bool
button_chord_matches(const struct StateTransition* t)
{
  return (ctx.event_type == EventTypeBtnPress || ctx.event_type == EventTypeBtnRelease) &&
         t->mod == ctx.btn_mod && t->btn == ctx.btn;
}

/*
 * The row of the transition table that matches what the context holds, or NULL
 * when no row does.
 */
static const struct StateTransition*
find_transition(void)
{
  const int num_transitions = (int) (sizeof(transitions) / sizeof(transitions[0]));

  for (int i = 0; i < num_transitions; i++) {
    const struct StateTransition* t = &transitions[i];

    if (t->window_target_type != ctx.window_target_type || t->event_type != ctx.event_type ||
        t->prev_state != ctx.state)
      continue;

    if (key_chord_matches(t) || button_chord_matches(t))
      return t;
  }

  return NULL;
}

void
handle_state_event(xcb_generic_event_t* event)
{
  xcb_window_t hover_wnd = XCB_NONE;
  bool         handled   = false;

  switch (event->response_type) {
  case XCB_KEY_PRESS:
    context_from_key((const xcb_key_press_event_t*) event, EventTypeKeyPress);
    handled = true;
    break;
  case XCB_KEY_RELEASE:
    context_from_key((const xcb_key_press_event_t*) event, EventTypeKeyRelease);
    handled = true;
    break;
  case XCB_BUTTON_PRESS:
    context_from_button((const xcb_button_press_event_t*) event, EventTypeBtnPress);
    handled = true;
    break;
  case XCB_BUTTON_RELEASE:
    context_from_button((const xcb_button_press_event_t*) event, EventTypeBtnRelease);
    handled = true;
    break;
  case XCB_ENTER_NOTIFY:
    hover_wnd = ((const xcb_enter_notify_event_t*) event)->event;
    break;
  default:
    /* Pointer motion, focus and every other event carry nothing the table
     * reads. */
    break;
  }

  ctx.window_target_type = WndTypeNone;
  if (hover_wnd == root)
    ctx.window_target_type = WndTypeRoot;
  else if (hover_wnd != XCB_NONE)
    ctx.window_target_type = WndTypeClient;

  if (!handled)
    return;

  LOG_DEBUG("Finding transition for event, with following properties:\n\troot: %d\n\tchild:%d\n\twnd_target_type: %d\n\tevent_type: %s\n\tkey_mod: %s\n\tkey: %d\n\tbtn_mod: %s\n\tbtn: %d\n\tstate: %d",
            ctx.root,
            ctx.child,
            ctx.window_target_type,
            event_type_name(ctx.event_type),
            mod_key_name(ctx.key_mod),
            ctx.key,
            mod_key_name(ctx.btn_mod),
            ctx.btn,
            ctx.state);

  const struct StateTransition* transition = find_transition();
  if (transition != NULL)
    ctx.state = transition->next_state;

  ctx.state_funcs[ctx.state](&ctx);
}

void
state_idle(struct Context* ctx)
{
  // LOG_DEBUG("State: Idle.");
}

void
state_window_move(struct Context* ctx)
{
  LOG_DEBUG("Action: Window movement.");
}

void
state_window_resize(struct Context* ctx)
{
  LOG_DEBUG("Action: Window resizing.");
}

void
setup_state_machine()
{
}

void
destruct_state_machine()
{
}
