#ifndef JES_UTIL_H
#define JES_UTIL_H

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "jes.h"

static char jes_token_type_str[][20] = {
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

static char jes_node_type_str[][20] = {
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
static char jes_state_str[][20] = {
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
  #define JES_LOG_TOKEN jes_log_token
  #define JES_LOG_NODE  jes_log_node
  #define JES_LOG_MSG   jes_log_msg
#else
  #define JES_LOG_TOKEN(...)
  #define JES_LOG_NODE(...)
  #define JES_LOG_MSG(...)
#endif
static inline void jes_log_token(uint16_t token_type, uint32_t token_pos, uint32_t token_len, uint8_t *token_value)
{
  printf("\n JES.Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token_pos, token_len, jes_token_type_str[token_type],
          token_len, token_value);
}

static inline void jes_log_node(int16_t node_id, uint32_t node_type, int16_t parent_id, int16_t right_id, int16_t child_id)
{
  printf("\n   + JES.Node: [%d] %s, parent:[%d], right:[%d], child:[%d]\n", node_id, jes_node_type_str[node_type], parent_id, right_id, child_id);
}

static inline void jes_log_msg(char *msg)
{
  printf(" JSE: %s\n", msg);
}

#endif /* JES_UTIL_H */