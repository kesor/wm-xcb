/*
 * config.c - Configuration system
 *
 * Provides read-only access to configuration values.
 * Configuration is compile-time only (config.def.h).
 *
 * Note: Keybindings are NOT handled here. They're registered
 * directly via keybinding_binding_register() calls in config.wm.def.h,
 * keeping this module generic and decoupled from input handling.
 */

#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.def.h"
#include "config.wm.def.h"
#include "wm-log.h"

/*
 * Initialize the configuration system.
 * This wires keybindings from config.wm.def.h.
 */
void
config_init(void)
{
  LOG_DEBUG("Initializing configuration");
  config_wire_keybindings();
}

/*
 * Check if a string matches a pattern (substring match).
 * If pattern is NULL or empty, it matches everything.
 */
static bool
string_matches(const char* str, const char* pattern)
{
  if (pattern == NULL || pattern[0] == '\0') {
    return true; /* No pattern = match everything */
  }

  if (str == NULL || str[0] == '\0') {
    return false; /* Have pattern but no string to match */
  }

  return strstr(str, pattern) != NULL;
}

/*
 * Match a rule against window properties
 */
static bool
rule_matches(
    const char* class_name,
    const char* instance_name,
    const char* title,
    const char* rule_class,
    const char* rule_instance,
    const char* rule_title)
{
  /* Check class_name match */
  if (!string_matches(class_name, rule_class)) {
    return false;
  }

  /* Check instance_name match */
  if (!string_matches(instance_name, rule_instance)) {
    return false;
  }

  /* Check title match */
  if (!string_matches(title, rule_title)) {
    return false;
  }

  return true;
}

/*
 * ============================================================================
 * Configuration Access Functions
 * ============================================================================
 */

const FloatRule*
config_get_float_rules(void)
{
  return default_float_rules;
}

uint32_t
config_get_float_rule_count(void)
{
  return DEFAULT_FLOAT_RULE_COUNT;
}

const TagRule*
config_get_tag_rules(void)
{
  return default_tag_rules;
}

uint32_t
config_get_tag_rule_count(void)
{
  return DEFAULT_TAG_RULE_COUNT;
}

const GapConfig*
config_get_gaps(void)
{
  return &default_gaps;
}

const BorderConfig*
config_get_borders(void)
{
  return &default_borders;
}

uint32_t
config_get_tag_count(void)
{
  return CONFIG_TAG_NUM_TAGS;
}

/*
 * ============================================================================
 * Rule Matching Functions
 * ============================================================================
 */

bool
config_should_float(const char* class_name, const char* instance_name, const char* title)
{
  /* Check all float rules */
  for (uint32_t i = 0; i < DEFAULT_FLOAT_RULE_COUNT; i++) {
    const FloatRule* rule = &default_float_rules[i];

    /* Empty rule (all NULL) - skip (allows matching all windows) */
    if (rule->class_name == NULL && rule->instance_name == NULL && rule->title_substring == NULL) {
      LOG_DEBUG("Float rule %u: empty rule (skipping)", i);
      continue;
    }

    if (rule_matches(class_name, instance_name, title,
                     rule->class_name, rule->instance_name, rule->title_substring)) {
      LOG_DEBUG("Float rule %u matches: class=%s instance=%s title=%s",
                i, class_name ? class_name : "(null)",
                instance_name ? instance_name : "(null)",
                title ? title : "(null)");
      return true;
    }
  }

  return false;
}

uint32_t
config_get_tags(const char* class_name, const char* instance_name, const char* title)
{
  /* Check all tag rules - first match wins */
  for (uint32_t i = 0; i < DEFAULT_TAG_RULE_COUNT; i++) {
    const TagRule* rule = &default_tag_rules[i];

    if (rule_matches(class_name, instance_name, title,
                     rule->class_name, rule->instance_name, rule->title_substring)) {
      LOG_DEBUG("Tag rule %u matches: class=%s instance=%s title=%s -> tags=%" PRIu32,
                i, class_name ? class_name : "(null)",
                instance_name ? instance_name : "(null)",
                title ? title : "(null)", rule->tags);
      return rule->tags;
    }
  }

  /* No matching rule - use default */
  return CONFIG_TAG_DEFAULT_MASK;
}