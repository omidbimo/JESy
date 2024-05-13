#ifndef JESY_H
#define JESY_H

#include <stdbool.h>
#include <stdint.h>

/* Comment or undef to disable searching for duplicate keys and overwriting
   their values if you're sure that there will be no duplicate keys in the
   source JSON.
   Disabling JESY_OVERWRITE_DUPLICATE_KEYS, will considerably improve the
   parsing performance.
   */
#define JESY_OVERWRITE_DUPLICATE_KEYS

struct jesy_parser_context; /* Forward declaration */

typedef enum jesy_status {
  JESY_NO_ERR = 0,
  JESY_PARSING_FAILED,
  JESY_ALLOCATION_FAILED,
  JESY_UNEXPECTED_TOKEN,
} jesy_status;

struct jesy_context {
  jesy_status  status;
  uint32_t    node_count;
  uint32_t    required_mem;
  uint32_t    dump_size;
  struct jesy_parser_context *pacx;
};

enum jesy_node_type {
  JESY_NONE = 0,
  JESY_OBJECT,
  JESY_KEY,
  JESY_ARRAY,
  JESY_VALUE_STRING,
  JESY_VALUE_NUMBER,
  JESY_VALUE_BOOLEAN,
  JESY_VALUE_NULL,
};

struct jessy_element {
  uint16_t type; /* of type enum jesy_node_type */
  uint16_t length;
  char    *value;
};

struct jesy_context* jesy_init_context(void *mem_pool, uint32_t pool_size);
jesy_status jesy_parse(struct jesy_context* ctx, char *json_data, uint32_t json_length);
jesy_status jesy_serialize(struct jesy_context *ctx, char *json_data, uint32_t length);

void jesy_reset_iterator(struct jesy_context *ctx);

struct jessy_element jesy_get_root(struct jesy_context *ctx);
struct jessy_element jesy_get_parent(struct jesy_context *ctx);
struct jessy_element jesy_get_child(struct jesy_context *ctx);
struct jessy_element jesy_get_next(struct jesy_context *ctx);
void jesy_print(struct jesy_context *ctx);

struct jesy_element jesy_get(struct jesy_context *ctx, char *key);
bool jesy_find(struct jesy_context *ctx, char *key);
bool jesy_has(struct jesy_context *ctx, char *key);
bool jesy_set(struct jesy_context *ctx, char *key, char *value, uint16_t length);
enum jesy_node_type jesy_get_type(struct jesy_context *ctx, char *key);

#endif