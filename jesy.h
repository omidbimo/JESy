#ifndef JESY_H
#define JESY_H

#define FOR_EACH()

struct jesy_parser_context; /* Forward declaration */
typedef uint32_t jesy_status;

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

struct jesy_json_element {
  uint16_t type; /* of type enum jesy_node_type */
  uint16_t length;
  char    *value;
};

struct jesy_context* jesy_init_context(void *mem_pool, uint32_t pool_size);
jesy_status jesy_parse(struct jesy_context* ctx, char *json_data, uint32_t json_length);
jesy_status jesy_serialize(struct jesy_context *ctx, char *json_data, uint32_t length);

struct jesy_json_element jesy_get_key(struct jesy_context *ctx, char *name);
void jesy_reset_iterator(struct jesy_context *ctx);
struct jesy_json_element jesy_get_root(struct jesy_context *ctx);
struct jesy_json_element jesy_get_parent(struct jesy_context *ctx);
struct jesy_json_element jesy_get_child(struct jesy_context *ctx);
struct jesy_json_element jesy_get_next(struct jesy_context *ctx);
void jesy_print(struct jesy_context *ctx);
#endif