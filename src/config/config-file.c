/*
 * config-file.c - parsing the user's configuration file
 *
 * See config-file.h for the format. Everything here is pure parsing: no X, no
 * globals, no state that outlives a ConfigFile.
 */

#include "src/config/config-file.h"

#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include <xcb/xcb.h>
#include <X11/keysym.h>

#include "src/core/wm-log.h"

/*
 * One line of the file may carry this many whitespace-separated fields. The
 * longest line the format defines is `tags` with nine names, and `bind` with a
 * chord, an action and an argument; anything above this is a typo, not a line
 * the parser failed on.
 */
#define CONFIG_FILE_MAX_TOKENS 12

/* A `bind` chord is at most a key plus a handful of modifiers. */
#define CONFIG_FILE_CHORD_MAX  96

/*
 * Modifier names a chord may use, and the XCB mask each one means. `mod` is
 * the primary modifier of the compiled configuration, so a file that says
 * `Mod+1` binds what the defaults bind.
 *
 * Mod2 is not here. NumLock is Mod2 on a standard keymap, and the binding
 * lookup masks that bit out of both sides of the comparison, so a chord naming
 * it would register as the same chord without it: `bind Mod2+Shift+1` is
 * `bind Shift+1`. parse_chord() refuses the name so the user reads why instead
 * of getting a chord they did not write.
 */
typedef struct ModifierName {
  const char* name;
  uint32_t    mask;
} ModifierName;

static const ModifierName modifier_names[] = {
  { "mod",     XCB_MOD_MASK_4       },
  { "mod4",    XCB_MOD_MASK_4       },
  { "super",   XCB_MOD_MASK_4       },
  { "win",     XCB_MOD_MASK_4       },
  { "shift",   XCB_MOD_MASK_SHIFT   },
  { "ctrl",    XCB_MOD_MASK_CONTROL },
  { "control", XCB_MOD_MASK_CONTROL },
  { "alt",     XCB_MOD_MASK_1       },
  { "mod1",    XCB_MOD_MASK_1       },
  { "mod3",    XCB_MOD_MASK_3       },
  { "mod5",    XCB_MOD_MASK_5       },
};

/*
 * Keys whose name is not a single character. Single printable characters need
 * no entry: their Latin-1 keysym is the character itself.
 */
typedef struct KeyName {
  const char* name;
  uint32_t    keysym;
} KeyName;

static const KeyName key_names[] = {
  { "return",    XK_Return    },
  { "enter",     XK_Return    },
  { "space",     XK_space     },
  { "tab",       XK_Tab       },
  { "escape",    XK_Escape    },
  { "esc",       XK_Escape    },
  { "backspace", XK_BackSpace },
  { "delete",    XK_Delete    },
  { "insert",    XK_Insert    },
  { "home",      XK_Home      },
  { "end",       XK_End       },
  { "pageup",    XK_Page_Up   },
  { "page_up",   XK_Page_Up   },
  { "pagedown",  XK_Page_Down },
  { "page_down", XK_Page_Down },
  { "left",      XK_Left      },
  { "right",     XK_Right     },
  { "up",        XK_Up        },
  { "down",      XK_Down      },
  { "print",     XK_Print     },
  { "pause",     XK_Pause     },
  { "menu",      XK_Menu      },
  { "f1",        XK_F1        },
  { "f2",        XK_F2        },
  { "f3",        XK_F3        },
  { "f4",        XK_F4        },
  { "f5",        XK_F5        },
  { "f6",        XK_F6        },
  { "f7",        XK_F7        },
  { "f8",        XK_F8        },
  { "f9",        XK_F9        },
  { "f10",       XK_F10       },
  { "f11",       XK_F11       },
  { "f12",       XK_F12       },
};

/*
 * Record a rejected line. The text is what config_load_runtime() prints, so it
 * names the thing the user has to change, not the parser's rule.
 */
static void
record_error(ConfigFile* cf, int line, const char* fmt, ...)
{
  if (cf->error_count < CONFIG_FILE_MAX_ERRORS) {
    ConfigError* err = &cf->errors[cf->error_count];
    va_list      ap;

    err->line = line;
    va_start(ap, fmt);
    (void) vsnprintf(err->text, sizeof err->text, fmt, ap);
    va_end(ap);
  }
  cf->error_count++;
}

static bool
is_blank(char c)
{
  return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

/* Step over the blanks between fields. */
static void
skip_blanks(char** read)
{
  while (is_blank(**read))
    (*read)++;
}

/*
 * Copy one field from `read` to `write`, dropping the quotes and keeping what
 * they protected.
 *
 * Returns false when a quote is still open where the line ends: that field is
 * broken, not short, and the caller reports the line instead of using what is
 * left of it.
 */
static bool
read_field(char** read, char** write)
{
  bool quoted = false;

  while (**read != '\0') {
    char c = **read;

    if (c == '"') {
      quoted = !quoted;
      (*read)++;
      continue;
    }
    if (!quoted && (is_blank(c) || c == '#'))
      break;

    *(*write)++ = c;
    (*read)++;
  }

  return !quoted;
}

/*
 * Split a line into whitespace-separated fields. Double quotes inside a field
 * keep the spaces in it (`float title=" - Preferences"`); the quotes
 * themselves are not part of the field. A `#` outside quotes begins a comment
 * and ends the line.
 *
 * The fields are written back into `line`, from `write` which never passes
 * `read`, so the caller's buffer holds them in place.
 *
 * Returns the field count, or -1 when a quote is not closed.
 */
static int
tokenize(char* line, char** tokens, int max)
{
  char* read  = line;
  char* write = line;
  int   count = 0;

  while (true) {
    skip_blanks(&read);
    if (*read == '\0' || *read == '#')
      break;
    if (count == max)
      return count;

    tokens[count++] = write;
    if (!read_field(&read, &write))
      return -1;

    bool at_end  = *read == '\0';
    bool comment = *read == '#';

    *write++ = '\0';
    if (comment)
      break;
    if (!at_end)
      read++;
  }

  return count;
}

static uint32_t
modifier_mask(const char* name)
{
  for (size_t i = 0; i < sizeof(modifier_names) / sizeof(modifier_names[0]); i++) {
    if (strcasecmp(name, modifier_names[i].name) == 0)
      return modifier_names[i].mask;
  }
  return 0;
}

uint32_t
config_file_key_keysym(const char* name)
{
  if (name == NULL || name[0] == '\0')
    return 0;

  if (name[1] == '\0') {
    unsigned char c = (unsigned char) name[0];

    /* An uppercase letter names the same key as the lowercase one: the shift
     * that produces the capital is a modifier the chord has to ask for. */
    if (c >= 'A' && c <= 'Z')
      c = (unsigned char) (c - 'A' + 'a');
    if (c > ' ' && c < 0x7f)
      return (uint32_t) c;
    return 0;
  }

  for (size_t i = 0; i < sizeof(key_names) / sizeof(key_names[0]); i++) {
    if (strcasecmp(name, key_names[i].name) == 0)
      return key_names[i].keysym;
  }
  return 0;
}

static bool
parse_arg(const char* text, uint32_t* out)
{
  char*         end   = NULL;
  unsigned long value = 0;

  if (text[0] == '-' || text[0] == '+')
    return false;

  errno = 0;
  value = strtoul(text, &end, 10);
  if (end == text || *end != '\0' || errno == ERANGE || value > UINT32_MAX)
    return false;

  *out = (uint32_t) value;
  return true;
}

/*
 * Resolve one modifier of a chord into its mask. A name that is not in the
 * table is recorded as the reason it cannot be used, which is one message for
 * Mod2 and another for a name nobody knows.
 */
static bool
chord_modifier(ConfigFile* cf, int line, const char* part, const char* chord, uint32_t* mask)
{
  *mask = modifier_mask(part);
  if (*mask != 0)
    return true;

  if (strcasecmp(part, "mod2") == 0) {
    record_error(cf, line,
                 "Mod2 is NumLock on this keymap, so '%s' cannot be told apart from the "
                 "same chord without it: write the chord without Mod2",
                 chord);
    return false;
  }

  /* Every name in modifier_names[], in that order, including the aliases.
   * This string is a second copy of that table. A shorter hint omits a name
   * the parser accepts and the docs list. */
  record_error(cf, line,
               "unknown modifier '%s' in '%s': use Mod, Mod4, Super, Win, Shift, Ctrl, Control, Alt, "
               "Mod1, Mod3 or Mod5",
               part, chord);
  return false;
}

/*
 * Parse a chord such as `Mod+Shift+1` into a modifier mask and a keysym.
 *
 * A chord has to name at least one modifier. A grab on a bare key takes that
 * key away from every client, so `bind Return terminal.spawn` would stop every
 * application in the session from seeing Return, which is not what a config
 * file is for.
 */
static bool
parse_chord(ConfigFile* cf, int line, const char* chord, ConfigBind* bind)
{
  char     text[CONFIG_FILE_CHORD_MAX];
  char*    part      = text;
  uint32_t modifiers = 0;

  if (strlen(chord) >= sizeof text) {
    record_error(cf, line, "the key chord is longer than %zu bytes", sizeof text - 1);
    return false;
  }
  (void) snprintf(text, sizeof text, "%s", chord);

  for (;;) {
    char* plus = strchr(part, '+');

    if (plus != NULL)
      *plus = '\0';

    if (part[0] == '\0') {
      record_error(cf, line, "'%s' has an empty key or modifier in it", chord);
      return false;
    }

    if (plus == NULL) {
      uint32_t keysym = config_file_key_keysym(part);

      if (keysym == 0) {
        record_error(cf, line, "unknown key '%s' in '%s'", part, chord);
        return false;
      }
      if (strlen(part) >= sizeof bind->key) {
        record_error(cf, line, "the key name is longer than %zu bytes", sizeof bind->key - 1);
        return false;
      }
      bind->keysym = keysym;
      memcpy(bind->key, part, strlen(part) + 1);
      break;
    }

    uint32_t mask = 0;
    if (!chord_modifier(cf, line, part, chord, &mask))
      return false;

    modifiers |= mask;
    part = plus + 1;
  }

  if (modifiers == 0) {
    record_error(cf, line, "'%s' has no modifier: a binding on a plain key would take that key from every client",
                 chord);
    return false;
  }

  bind->modifiers = modifiers;
  return true;
}

static void
parse_bind(ConfigFile* cf, int line, char** tokens, int count)
{
  ConfigBind bind = { 0 };

  if (count < 3 || count > 4) {
    record_error(cf, line, "bind takes a chord, an action and an optional argument: bind Mod+1 tag-manager.view 1");
    return;
  }
  if (cf->bind_count >= CONFIG_FILE_MAX_BINDS) {
    record_error(cf, line, "more than %d bindings", CONFIG_FILE_MAX_BINDS);
    return;
  }
  if (strlen(tokens[2]) >= sizeof bind.action) {
    record_error(cf, line, "the action name is longer than %zu bytes", sizeof bind.action - 1);
    return;
  }

  bind.line = line;
  if (!parse_chord(cf, line, tokens[1], &bind))
    return;

  (void) snprintf(bind.action, sizeof bind.action, "%s", tokens[2]);

  if (count == 4) {
    if (!parse_arg(tokens[3], &bind.arg)) {
      record_error(cf, line, "'%s' is not a number, which is what an action argument has to be", tokens[3]);
      return;
    }
    bind.has_arg = true;
  }

  cf->binds[cf->bind_count++] = bind;
}

static void
free_float_rule(ConfigFloatRule* rule)
{
  free(rule->class_name);
  free(rule->instance_name);
  free(rule->title_substring);
  rule->class_name      = NULL;
  rule->instance_name   = NULL;
  rule->title_substring = NULL;
}

static bool
set_float_field(ConfigFile* cf, int line, ConfigFloatRule* rule, const char* key, const char* value)
{
  char** field = NULL;

  if (strcasecmp(key, "class") == 0)
    field = &rule->class_name;
  else if (strcasecmp(key, "instance") == 0)
    field = &rule->instance_name;
  else if (strcasecmp(key, "title") == 0)
    field = &rule->title_substring;
  else {
    record_error(cf, line, "unknown float field '%s': use class=, instance= or title=", key);
    return false;
  }

  free(*field);
  *field = strdup(value);
  if (*field == NULL) {
    record_error(cf, line, "out of memory while reading the rule");
    return false;
  }
  return true;
}

static void
parse_float(ConfigFile* cf, int line, char** tokens, int count)
{
  ConfigFloatRule rule = { 0 };

  if (count < 2) {
    record_error(cf, line, "float needs at least one of class=<name>, instance=<name> or title=<text>");
    return;
  }
  if (cf->float_rule_count >= CONFIG_FILE_MAX_FLOAT_RULES) {
    record_error(cf, line, "more than %d float rules", CONFIG_FILE_MAX_FLOAT_RULES);
    return;
  }

  rule.line = line;
  for (int i = 1; i < count; i++) {
    char* equals = strchr(tokens[i], '=');

    if (equals == NULL) {
      record_error(cf, line, "'%s' is not field=value: use class=, instance= or title=", tokens[i]);
      free_float_rule(&rule);
      return;
    }

    *equals = '\0';
    if (equals[1] == '\0') {
      record_error(cf, line, "float field '%s' has no value", tokens[i]);
      free_float_rule(&rule);
      return;
    }
    if (!set_float_field(cf, line, &rule, tokens[i], equals + 1)) {
      free_float_rule(&rule);
      return;
    }
  }

  cf->float_rules[cf->float_rule_count++] = rule;
}

static void
parse_tags(ConfigFile* cf, int line, char** tokens, int count)
{
  int   names                       = count - 1;
  char* fresh[CONFIG_FILE_MAX_TAGS] = { NULL };

  if (names == 0) {
    record_error(cf, line, "tags needs at least one name");
    return;
  }
  if (names > CONFIG_FILE_MAX_TAGS) {
    record_error(cf, line, "%d tag names, but the WM has %d tags", names, CONFIG_FILE_MAX_TAGS);
    return;
  }

  for (int i = 0; i < names; i++) {
    if (tokens[i + 1][0] == '\0') {
      record_error(cf, line, "tag %d has an empty name", i + 1);
      for (int j = 0; j < i; j++)
        free(fresh[j]);
      return;
    }
    fresh[i] = strdup(tokens[i + 1]);
    if (fresh[i] == NULL) {
      record_error(cf, line, "out of memory while reading the tag names");
      for (int j = 0; j < i; j++)
        free(fresh[j]);
      return;
    }
  }

  if (cf->tags_set)
    LOG_WARN("config: the tags line at line %d replaces the one at line %d", line, cf->tags_line);

  for (int i = 0; i < CONFIG_FILE_MAX_TAGS; i++) {
    free(cf->tag_names[i]);
    cf->tag_names[i] = fresh[i];
  }
  cf->tags_set  = true;
  cf->tags_line = line;
}

static void
parse_line(ConfigFile* cf, int line, char* text)
{
  char* tokens[CONFIG_FILE_MAX_TOKENS] = { NULL };
  int   count                          = tokenize(text, tokens, CONFIG_FILE_MAX_TOKENS);

  if (count < 0) {
    record_error(cf, line, "a quoted value is not closed");
    return;
  }
  if (count == 0)
    return;
  if (count == CONFIG_FILE_MAX_TOKENS) {
    record_error(cf, line, "more than %d fields on the line", CONFIG_FILE_MAX_TOKENS - 1);
    return;
  }

  if (strcmp(tokens[0], "bind") == 0)
    parse_bind(cf, line, tokens, count);
  else if (strcmp(tokens[0], "float") == 0)
    parse_float(cf, line, tokens, count);
  else if (strcmp(tokens[0], "tags") == 0)
    parse_tags(cf, line, tokens, count);
  else
    record_error(cf, line, "unknown directive '%s': use bind, float or tags", tokens[0]);
}

ConfigFile*
config_file_load(const char* path)
{
  ConfigFile* cf = calloc(1, sizeof *cf);

  if (cf == NULL) {
    LOG_ERROR("out of memory reading the config file %s", path);
    return NULL;
  }

  FILE* file = fopen(path, "r");
  if (file == NULL) {
    if (errno != ENOENT)
      LOG_ERROR("cannot read the config file %s: %s", path, strerror(errno));
    return cf;
  }

  cf->found = true;
  char line[1024];
  int  line_number = 0;

  while (fgets(line, sizeof line, file) != NULL) {
    size_t length = strlen(line);
    int    ch     = 0;

    line_number++;
    if (length > 0 && line[length - 1] == '\n') {
      line[length - 1] = '\0';
    } else if (!feof(file)) {
      record_error(cf, line_number, "the line is longer than %zu bytes", sizeof line - 2);
      while ((ch = fgetc(file)) != EOF && ch != '\n')
        ;
      continue;
    }
    parse_line(cf, line_number, line);
  }

  (void) fclose(file);
  return cf;
}

void
config_file_free(ConfigFile* cf)
{
  if (cf == NULL)
    return;

  for (uint32_t i = 0; i < cf->float_rule_count; i++)
    free_float_rule(&cf->float_rules[i]);
  for (int i = 0; i < CONFIG_FILE_MAX_TAGS; i++)
    free(cf->tag_names[i]);
  free(cf);
}

bool
config_file_default_path(char* buffer, size_t size)
{
  const char* base   = getenv("WM_CONFIG");
  int         length = 0;

  if (base != NULL && base[0] != '\0') {
    length = snprintf(buffer, size, "%s", base);
    return length >= 0 && (size_t) length < size;
  }

  base = getenv("XDG_CONFIG_HOME");
  if (base != NULL && base[0] != '\0') {
    length = snprintf(buffer, size, "%s/wm/config", base);
    return length >= 0 && (size_t) length < size;
  }

  base = getenv("HOME");
  if (base != NULL && base[0] != '\0') {
    length = snprintf(buffer, size, "%s/.config/wm/config", base);
    return length >= 0 && (size_t) length < size;
  }

  return false;
}
