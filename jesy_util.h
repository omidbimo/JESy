#ifndef JESY_UTIL_H
#define JESY_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "jesy.h"

static char jesy_token_type_str[][20] = {
  "EOF             ",
  "OPENING_BRACKET ",
  "CLOSING_BRACKET ",
  "OPENING_BRACE   ",
  "CLOSING_BRACE   ",
  "STRING          ",
  "NUMBER          ",
  "BOOLEAN         ",
  "NULL            ",
  "COLON           ",
  "COMMA           ",
  "ESC             ",
  "INVALID         ",
};

static char jesy_node_type_str[][20] = {
  "NONE",
  "OBJECT",
  "KEY",
  "ARRAY",
  "VALUE_STRING",
  "VALUE_NUMBER",
  "VALUE_BOOLEAN",
  "VALUE_NULL",
};

#if 1
static char jesy_state_str[][20] = {
  "STATE_START",
  "STATE_WANT_KEY",
  "STATE_WANT_VALUE",
  "STATE_WANT_ARRAY",
  "STATE_PROPERTY_END",
  "STATE_VALUE_END",
  "STATE_STRUCTURE_END",
};
#endif

#ifndef NDEBUG
  #define JESY_LOG_TOKEN jesy_log_token
  #define JESY_LOG_NODE  jesy_log_node
  #define JESY_LOG_MSG   jesy_log_msg
#else
  #define JESY_LOG_TOKEN(...)
  #define JESY_LOG_NODE(...)
  #define JESY_LOG_MSG(...)
#endif
static inline void jesy_log_token(uint16_t token_type, uint32_t token_pos, uint32_t token_len, uint8_t *token_value)
{
  printf("\n JES.Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token_pos, token_len, jesy_token_type_str[token_type],
          token_len, token_value);
}

static inline void jesy_log_node(int16_t node_id, uint32_t node_type, int16_t parent_id, int16_t right_id, int16_t child_id)
{
  printf("\n   + JES.Node: [%d] %s, parent:[%d], right:[%d], child:[%d]\n", node_id, jesy_node_type_str[node_type], parent_id, right_id, child_id);
}

static inline void jesy_log_msg(char *msg)
{
  printf(" JSE: %s\n", msg);
}

#endif /* JESY_UTIL_H */