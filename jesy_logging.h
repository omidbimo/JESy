#ifndef JESY_UTIL_H
#define JESY_UTIL_H

static char jesy_token_type_str[][20] = {
  "EOF",
  "OPENING_BRACKET",
  "CLOSING_BRACKET",
  "OPENING_BRACE",
  "CLOSING_BRACE",
  "STRING",
  "NUMBER",
  "TRUE",
  "FALSE",
  "NULL",
  "COLON",
  "COMMA",
  "ESC",
  "INVALID",
};

static char jesy_node_type_str[][20] = {
  "NONE",
  "OBJECT",
  "KEY",
  "ARRAY",
  "STRING_VALUE",
  "NUMBER_VALUE",
  "TRUE_VALUE",
  "FALSE_VALUE",
  "NULL_VALUE",
};

static char jesy_state_str[][20] = {
  "NONE",
  "WANT_OBJECT",
  "WANT_KEY",
  "WANT_VALUE",
  "WANT_ARRAY_VALUE",
  "GOT_ARRAY_VALUE",
  "GOT_VALUE",
  "GOT_KEY",
};

static char jesy_status_str[][20] = {
  "NO_ERR",
  "PARSING_FAILED",
  "RENDER_FAILED",
  "OUT_OF_MEMORY",
  "UNEXPECTED_TOKEN",
  "UNEXPECTED_NODE",
  "UNEXPECTED_EOF",
};

static inline void jesy_log_token(uint16_t token_type,
                                  uint32_t token_pos,
                                  uint32_t token_len,
                                  const uint8_t *token_value)
{
  printf("\n JESy.Token: [Pos: %5d, Len: %3d] %-16s \"%.*s\"",
          token_pos, token_len, jesy_token_type_str[token_type],
          token_len, token_value);
}

static inline void jesy_log_node( const char *pre_msg,
                                  int16_t node_id,
                                  uint32_t node_type,
                                  uint32_t node_length,
                                  const char *node_value,
                                  int16_t parent_id,
                                  int16_t right_id,
                                  int16_t child_id,
                                  const char *post_msg)
{
  printf("%sJESy.Node: [%d] \"%.*s\" <%s>,    parent:[%d], right:[%d], child:[%d]%s",
    pre_msg, node_id, node_length, node_value, jesy_node_type_str[node_type], parent_id, right_id, child_id, post_msg);
}

static inline void jesy_log_msg(char *msg)
{
  printf("\nJSEy: %s", msg);
}

#endif /* JESY_UTIL_H */