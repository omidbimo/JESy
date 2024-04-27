#ifndef JES_H
#define JES_H

struct jes_parser_context;

struct jes_context {
  uint32_t  error;
  uint32_t  node_count;
  struct jes_parser_context *pacx;
};

enum jes_node_type {
  JES_NODE_NONE = 0,
  JES_NODE_OBJECT,
  JES_NODE_KEY,
  JES_NODE_VALUE,
  JES_NODE_ARRAY,
};

struct jes_node {
  uint16_t type; /* of type enum jes_node_type */
  uint16_t size;
  uint32_t offset;
};

struct jes_context* jes_parse(char *json_data, uint32_t size,
                              void *mem_pool, uint32_t pool_size);

struct jes_node jes_root(struct jes_context *ctx);
struct jes_node jes_get_parent(struct jes_context *ctx);
struct jes_node jes_get_child(struct jes_context *ctx);
struct jes_node jes_get_next(struct jes_context *ctx);

#endif