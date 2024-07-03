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

/* Comment or undef to enable searching for duplicate keys and overwriting
 * their values.
 * Leaving JESY_ALLOW_DUPLICATE_KEYS enabled, has a positive impact on the
 * parsing performance when processing large documents. If key duplication is
 * not a big deal in your implementation, then relax the parser.
 */
#define JESY_ALLOW_DUPLICATE_KEYS

//#define JESY_USE_32BIT_NODE_DESCRIPTOR

typedef enum jesy_status {
  JESY_NO_ERR = 0,
  JESY_PARSING_FAILED,
  JESY_RENDER_FAILED,
  JESY_OUT_OF_MEMORY,
  JESY_UNEXPECTED_TOKEN,
  JESY_UNEXPECTED_NODE,
  JESY_UNEXPECTED_EOF,
  JESY_INVALID_PARAMETER,
  JESY_ELEMENT_NOT_FOUND,
} jesy_status;

enum jesy_token_type {
  JESY_TOKEN_EOF = 0,
  JESY_TOKEN_OPENING_BRACKET,
  JESY_TOKEN_CLOSING_BRACKET,
  JESY_TOKEN_OPENING_BRACE,
  JESY_TOKEN_CLOSING_BRACE,
  JESY_TOKEN_STRING,
  JESY_TOKEN_NUMBER,
  JESY_TOKEN_TRUE,
  JESY_TOKEN_FALSE,
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
  JESY_TRUE,
  JESY_FALSE,
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
  const char *value;
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
  const char *json_data;
  /* Length of JSON data in bytes. */
  uint32_t  json_size;
  /* Offset of the next symbol in the input JSON data Tokenizer is going to consume. */
  uint32_t  offset;
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first. Then
   * the remaining will be used as a pool of nodes (max. 65535 nodes).
   * Actually the pool member points to the memory after context. */
   struct jesy_element *pool;
  /* Pool size in bytes. It is limited to 32-bit value which is more than what
   * most of embedded systems can provide. */
  uint32_t pool_size;
  /* Number of nodes that can be allocated on the given buffer. The value will
     be limited to 65535 in case of 16-bit node descriptors. */
  uint32_t capacity;
  /* Index of the last allocated node */
  jesy_node_descriptor  index;
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

/* Initialize a new JESy context. The context contains the required data for both
 * parser and renderer.
 * param [in] mem_pool a buffer to hold the context and JSON tree nodes
 * param [in] pool_size size of the mem_pool must be at least the size of context
 *
 * return pointer to context or NULL in case of a failure.
 */
struct jesy_context* jesy_init_context(void *mem_pool, uint32_t pool_size);

/* Parse a string JSON and generate a tree of JSON elements.
 * param [in] ctx is an initialized context
 * param [in] json_data in form of string no need to be NUL terminated.
 * param [in] json_length is the size of json to be parsed.
 *
 * return status of the parsing process see: enum jesy_status
 *
 * note: the return value is also available in ctx->status
 */
uint32_t jesy_parse(struct jesy_context* ctx, const char *json_data, uint32_t json_length);

/* Render a tree of JSON elements into the destination buffer as a non-NUL terminated string.
 * param [in] ctx the Jesy context containing a JSON tree.
 * param [in] dst the destination buffer to hold the JSON string.
 * param [in] length is the size of destination buffer in bytes.
 *
 * return the size of JSON string. If zero, there where probably a failure. Check the ctx->status
 *
 * note: The output JSON is totally compact without any space characters.
 * note: It's possible to get the size of JSON string by calling the evaluate function.
 */
uint32_t jesy_render(struct jesy_context *ctx, char *dst, uint32_t length);

/* Evaluates a tree of JSON elements to check if the structure is correct. Additionally
 * calculates the size of the rendered JSON.
 * param [in] ctx the Jesy context containing a JSON tree.
 *
 * return the required buffer size to render the JSON into string. If zero,
          there might be failures in the tree. Check ctx->status.
 */
uint32_t jesy_evaluate(struct jesy_context *ctx);

/* Deletes an element, containing all of its sub-elements. */
void jesy_delete_element(struct jesy_context *ctx, struct jesy_element *element);

/* Delivers the root element of the JSOn tree.
 * Returning a NULL is meaning that the tree is empty. */
struct jesy_element* jesy_get_root(struct jesy_context *ctx);
/* Delivers the parent element of given JSON element */
struct jesy_element* jesy_get_parent(struct jesy_context *ctx, struct jesy_element *element);
/* Delivers the first child of given JSON element */
struct jesy_element* jesy_get_child(struct jesy_context *ctx, struct jesy_element *element);
/* Delivers the sibling of given JSON element */
struct jesy_element* jesy_get_sibling(struct jesy_context *ctx, struct jesy_element *element);

enum jesy_type jesy_get_parent_type(struct jesy_context *ctx, struct jesy_element *element);

/* Returns a Key element inside the given object.
 * param [in] ctx
 * param [in] object is a JSON element of type JESY_OBJECT
 * param [in] keys is a NUL-terminated string containing several key names separated by a dot "."
 *
 * return an element of type JESY_KEY or NULL if the key is not found.
 */
struct jesy_element* jesy_get_key(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* To get access to an object element giving keys value and the parent object.
 * return key's value if it's of type JESY_OBJECT or null */
struct jesy_element* jesy_get_object(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* To get access to an array element giving keys value and the parent object.
 * return key value if it's of type JESY_ARRAY or null */
struct jesy_element* jesy_get_array(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Get the number elements within an JESY_ARRAY element */
uint16_t jesy_get_array_size(struct jesy_context *ctx, struct jesy_element *array);
/* Returns value element of a given key name. NULL if element has no value yet.  */
struct jesy_element* jesy_get_key_value(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Returns value element of a given array element. NULL if element has no value yet. */
struct jesy_element* jesy_get_array_value(struct jesy_context *ctx, struct jesy_element *array, int32_t index);
/* Add an element to another element. */
struct jesy_element* jesy_add_element(struct jesy_context *ctx, struct jesy_element *parent, enum jesy_type type, const char *value);
/* Add a key to an object */
struct jesy_element* jesy_add_key(struct jesy_context *ctx, struct jesy_element *object, const char *keyword);
/* Update a key element giving its parent object.
 * note: The new key name will not be copied and must be non-retentive for the life time of jesy_context.
 * return a status code of type enum jesy_status */
uint32_t jesy_update_key(struct jesy_context *ctx, struct jesy_element *key, const char *keyword);
/* Update key value giving its name or name a series of keys separated with a dot
 * note: The new value will not be copied and must be non-retentive for the life time of jesy_context.
 * return a status code of type enum jesy_status */
uint32_t jesy_update_key_value(struct jesy_context *ctx, struct jesy_element *object, const char *keys, enum jesy_type type, const char *value);
/* Update the key value to a JESY_OBJECT element */
uint32_t jesy_update_key_value_object(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Update the key value to a JESY_ARRAY element */
uint32_t jesy_update_key_value_array(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Update the key value to a JESY_TRUE element */
uint32_t jesy_update_key_value_true(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Update the key value to a JESY_FALSE element */
uint32_t jesy_update_key_value_false(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Update the key value to a JESY_NULL element */
uint32_t jesy_update_key_value_null(struct jesy_context *ctx, struct jesy_element *object, const char *keys);
/* Update array value giving its array element and an index.
 * note: The new value will not be copied and must be non-retentive for the life time of jesy_context.
 * return a status code of type enum jesy_status */
uint32_t jesy_update_array_value(struct jesy_context *ctx, struct jesy_element *array, int16_t index, enum jesy_type type, const char *value);

#define JESY_FOR_EACH(ctx_, elem_, type_) for(elem_ = (elem_->type == type_) ? jesy_get_child(ctx_, elem_) : NULL; elem_ != NULL; elem_ = jesy_get_sibling(ctx_, elem_))
#define JESY_ARRAY_FOR_EACH(ctx_, array_, elem_) for(elem_ = (array_->type == JESY_ARRAY) ? jesy_get_child(ctx_, array_) : NULL; elem_ != NULL; elem_ = jesy_get_sibling(ctx_, elem_))

#endif