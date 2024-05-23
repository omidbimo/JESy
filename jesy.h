#ifndef JESY_H
#define JESY_H

#include <stdbool.h>
#include <stdint.h>

#ifndef NDEBUG
  #include "jesy_util.h"
  #define JESY_LOG_TOKEN jesy_log_token
  #define JESY_LOG_NODE  jesy_log_node
  #define JESY_LOG_MSG   jesy_log_msg
#else
  #define JESY_LOG_TOKEN(...)
  #define JESY_LOG_NODE(...)
  #define JESY_LOG_MSG(...)
#endif

/* Comment or undef to disable searching for duplicate keys and overwriting
   their values if you're sure that there will be no duplicate keys in the
   source JSON.
   Disabling JESY_OVERWRITE_DUPLICATE_KEYS, will considerably improve the
   parsing performance.
   */
#define JESY_OVERWRITE_DUPLICATE_KEYS

//#define JESY_USE_32BIT_NODE_DESCRIPTOR

typedef enum jesy_status {
  JESY_NO_ERR = 0,
  JESY_PARSING_FAILED,
  JESY_RENDER_FAILED,
  JESY_OUT_OF_MEMORY,
  JESY_UNEXPECTED_TOKEN,
  JESY_UNEXPECTED_NODE,
  JESY_UNEXPECTED_EOF,
} jesy_status;

enum jesy_parser_state {
  JESY_STATE_START = 0,
  JESY_STATE_WANT_KEY,
  JESY_STATE_WANT_VALUE,
  JESY_STATE_WANT_ARRAY,
  JESY_STATE_PROPERTY_END,   /* End of key:value pair */
  JESY_STATE_VALUE_END,      /* End of value inside an array */
  JESY_STATE_STRUCTURE_END,  /* End of object or array structure */
};

enum jesy_token_type {
  JESY_TOKEN_EOF = 0,
  JESY_TOKEN_OPENING_BRACKET,
  JESY_TOKEN_CLOSING_BRACKET,
  JESY_TOKEN_OPENING_BRACE,
  JESY_TOKEN_CLOSING_BRACE,
  JESY_TOKEN_STRING,
  JESY_TOKEN_NUMBER,
  JESY_TOKEN_BOOLEAN,
  JESY_TOKEN_NULL,
  JESY_TOKEN_COLON,
  JESY_TOKEN_COMMA,
  JESY_TOKEN_ESC,
  JESY_TOKEN_INVALID,
};

enum jesy_type {
  JESY_NONE = 0,
  JESY_OBJECT,
  JESY_KEY,
  JESY_ARRAY,
  JESY_STRING,
  JESY_NUMBER,
  JESY_BOOLEAN,
  JESY_NULL,
};

#ifdef JESY_USE_32BIT_NODE_DESCRIPTOR
/* A 32bit node descriptor limits the total number of nodes to 4294967295.
   Note that 0xFFFFFFFF is used as an invalid node index. */
typedef uint32_t jesy_node_descriptor;
#else
/* A 16bit node descriptor limits the total number of nodes to 65535.
   Note that 0xFFFF is used as an invalid node index. */
typedef uint16_t jesy_node_descriptor;
#endif

struct jesy_free_node {
  struct jesy_free_node *next;
};

/* An element is a TLV with additional members to track the its position in the
   JSON tree. */
struct jesy_element {
  /* Type of element. See jesy_type */
  uint16_t type;
  /* Length of value */
  uint16_t length;
  /* Value of element */
  char    *value;
  /* Index of the parent node. Each node holds the index of its parent. */
  jesy_node_descriptor parent;
  /* Index */
  jesy_node_descriptor sibling;
  /* Each parent keeps only the index of its first child. The remaining child nodes
     will be tracked using the right member of the first child. */
  jesy_node_descriptor first_child;
  /* The data member is a TLV (Type, Length, Value) which value is pointer to the
     actual value of the node. See jesy.h */
  /* Index */
  jesy_node_descriptor last_child;

};

struct jesy_token {
  enum jesy_token_type type;
  uint16_t length;
  uint32_t offset;
};

struct jesy_context {
  uint32_t status;
  /* Number of nodes in the current JSON */
  uint32_t node_count;
  /* JSON data to be parsed */
  char     *json_data;
  /* Length of JSON data in bytes. */
  uint32_t  json_size;
  /* Tokenizer uses this offset to track the consumed characters. */
  uint32_t  offset;
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first. Then
   * the remaining will be used as a pool of nodes (max. 65535 nodes).
   * Actually the pool member points to the memory after context. */
   struct jesy_element *pool;
  /* Pool size in bytes. It is limited to 32-bit value which is more than what
   * most of embedded systems can provide. */
  uint32_t  pool_size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  uint32_t  capacity;
  /* Index of the last allocated node */
  jesy_node_descriptor  index;
  /* */
  enum jesy_parser_state state;
  /* Holds the last token delivered by tokenizer. */
  struct jesy_token token;
  /* Internal node iterator */
  struct jesy_element *iter;
  /* Holds the main object node */
  struct jesy_element *root;
  /* Singly Linked list of freed nodes. This way the deleted nodes can be recycled
     by the allocator. */
  struct jesy_free_node *free;
};

struct jesy_context* jesy_init_context(void *mem_pool, uint32_t pool_size);
uint32_t jesy_parse(struct jesy_context* ctx, char *json_data, uint32_t json_length);
uint32_t jesy_serialize(struct jesy_context *ctx, char *dst, uint32_t length);

void jesy_reset_iterator(struct jesy_context *ctx);

struct jesy_element* jesy_get_root(struct jesy_context *ctx);
struct jesy_element* jesy_get_parent(struct jesy_context *ctx, struct jesy_element *element);
struct jesy_element* jesy_get_child(struct jesy_context *ctx, struct jesy_element *element);
struct jesy_element* jesy_get_sibling(struct jesy_context *ctx, struct jesy_element *element);
void jesy_print(struct jesy_context *ctx);

struct jesy_element* jesy_get_key_value(struct jesy_context *ctx, struct jesy_element *object, char *key);
struct jesy_element* jesy_get_array_value(struct jesy_context *ctx, struct jesy_element *array, int16_t index);

bool jesy_find(struct jesy_context *ctx, struct jesy_element *object, char *key);
bool jesy_has(struct jesy_context *ctx, struct jesy_element *object, char *key);
bool jesy_set(struct jesy_context *ctx, char *key, char *value, uint16_t length);
enum jesy_node_type jesy_get_type(struct jesy_context *ctx, char *key);
uint32_t jesy_get_dump_size(struct jesy_context *ctx);

#define JESY_FOR_EACH(ctx_, elem_, type_) for(elem_ = (elem_->type == type_) ? jesy_get_child(ctx_, elem_) : NULL; elem_ != NULL; elem_ = jesy_get_sibling(ctx_, elem_))
#define JESY_ARRAY_FOR_EACH(ctx_, elem_) for(elem_ = (elem_->type == JESY_ARRAY) ? jesy_get_child(ctx_, elem_) : NULL; elem_ != NULL; elem_ = jesy_get_sibling(ctx_, elem_))

#endif