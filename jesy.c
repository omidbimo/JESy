
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jesy.h"
#include "jesy_util.h"

#define JESY_INVALID_INDEX 0xFFFF

#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.length = size_;

#define LOOK_AHEAD(pacx_) pacx_->json_data[pacx_->offset + 1]
#define IS_EOF_AHEAD(pacx_) (((pacx_->offset + 1) >= pacx_->json_size) || \
                            (pacx_->json_data[pacx_->offset + 1] == '\0'))
#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))

#define HAS_CHILD(node_ptr) (node_ptr->child < JESY_INVALID_INDEX)
#define HAS_LEFT(node_ptr) (node_ptr->left < JESY_INVALID_INDEX)
#define HAS_RIGHT(node_ptr) (node_ptr->right < JESY_INVALID_INDEX)
#define HAS_PARENT(node_ptr) (node_ptr->parent < JESY_INVALID_INDEX)

#define GET_CONTEXT(pacx_)
#define GET_PARSER_CONTEXT(tnode_)
#define GET_TREE_NODE(node_)

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

enum jesy_parser_state {
  JESY_STATE_START = 0,
  JESY_STATE_WANT_KEY,
  JESY_STATE_WANT_VALUE,
  JESY_STATE_WANT_ARRAY,
  JESY_STATE_PROPERTY_END,   /* End of key:value pair */
  JESY_STATE_VALUE_END,      /* End of value inside an array */
  JESY_STATE_STRUCTURE_END,  /* End of object or array structure */
};

/* A 16bit node descriptor limits the total number of nodes to 65535.
   Note that 0xFFFF is used as an invalid node index. */
typedef uint16_t jesy_node_descriptor;

struct jesy_node {
  jesy_node_descriptor parent;
  jesy_node_descriptor left;
  jesy_node_descriptor right;
  jesy_node_descriptor child;
  struct jessy_element data;
};

struct jesy_free_node {
  struct jesy_free_node *next;
};

struct jesy_token {
  enum jesy_token_type type;
  uint16_t length;
  uint32_t offset;
};

struct jesy_parser_context {
  char     *json_data;
  uint32_t  json_size;
  /* Tokenizer uses this offset to track the consumed characters. */
  uint32_t  offset;
  /* Part of the buffer given by the user at the time of the context initialization.
   * The buffer will be used to allocate the context structure at first. Then
   * the remaining will be used as a pool of nodes (max. 65535 nodes).
   * Actually the pool member points to the memory after context. */
   struct jesy_node *pool;
  /* Pool size in bytes. It is limited to 32-bit value which is more than what
   * most of embedded systems can provide. */
  uint32_t  pool_size;
  /* Number of nodes that can be allocated on the given buffer.
     The value will be clamped to 65535 if the buffer can hold more nodes. */
  uint16_t  capacity;
  /* Number of nodes that are already allocated. */
  uint16_t  allocated;
  /* Index of the last allocated node */
  jesy_node_descriptor  index;
  /* */
  enum jesy_parser_state state;
  /* Holds the last token delivered by tokenizer. */
  struct jesy_token token;
  /* Internal node iterator */
  struct jesy_node *iter;
  /* Holds the main object node */
  struct jesy_node *root;
  /* Singly Linked list of freed nodes. This way the deleted nodes can be recycled
     by the allocator. */
  struct jesy_free_node *free;
};

static struct jesy_node *jesy_find_duplicated_key(struct jesy_parser_context *pacx,
              struct jesy_node *object_node, struct jesy_token *key_token);

static struct jesy_node* jesy_allocate(struct jesy_parser_context *pacx)
{
  struct jesy_node *new_node = NULL;

  if (pacx->allocated < pacx->capacity) {
    if (pacx->free) {
      /* Pop the first node from free list */
      new_node = (struct jesy_node*)pacx->free;
      pacx->free = pacx->free->next;
    }
    else {
      assert(pacx->index < pacx->capacity);
      new_node = &pacx->pool[pacx->index];
      pacx->index++;
    }
    /* Setting node descriptors to their default values. */
    memset(new_node, 0xFF, sizeof(jesy_node_descriptor) * 4);
    pacx->allocated++;
  }

  return new_node;
}

static void jesy_free(struct jesy_parser_context *pacx, struct jesy_node *node)
{
  struct jesy_free_node *free_node = (struct jesy_free_node*)node;

  assert(node >= pacx->pool);
  assert(node < (pacx->pool + pacx->capacity));
  assert(pacx->allocated > 0);

  if (pacx->allocated > 0) {
    free_node->next = NULL;
    pacx->allocated--;
    /* prepend the node to the free LIFO */
    if (pacx->free) {
      free_node->next = pacx->free->next;
    }
    pacx->free = free_node;
  }
}

static struct jesy_node* jesy_get_parent_node(struct jesy_parser_context *pacx,
                                                 struct jesy_node *node)
{
  /* TODO: add checking */
  if (node && (HAS_PARENT(node))) {
    return &pacx->pool[node->parent];
  }

  return NULL;
}

static struct jesy_node* jesy_get_parent_node_bytype(struct jesy_parser_context *pacx,
                                                     struct jesy_node *node,
                                                     enum jesy_node_type type)
{
  struct jesy_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &pacx->pool[node->parent];
    if (node->data.type == type) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jesy_node* jesy_get_structure_parent_node(struct jesy_parser_context *pacx,
                                                      struct jesy_node *node)
{
  struct jesy_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &pacx->pool[node->parent];
    if ((node->data.type == JESY_OBJECT) || (node->data.type == JESY_ARRAY)) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jesy_node* jesy_get_child_node(struct jesy_parser_context *pacx,
                                                struct jesy_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_CHILD(node)) {
    return &pacx->pool[node->child];
  }

  return NULL;
}

static struct jesy_node* jesy_get_right_node(struct jesy_parser_context *pacx,
                                                struct jesy_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_RIGHT(node)) {
    return &pacx->pool[node->right];
  }

  return NULL;
}

static struct jesy_node* jesy_add_node(struct jesy_parser_context *pacx,
                                          struct jesy_node *node,
                                          uint16_t type,
                                          uint32_t offset,
                                          uint16_t length)
{
  struct jesy_node *new_node = jesy_allocate(pacx);

  if (new_node) {
    new_node->data.type = type;
    new_node->data.length = length;
    new_node->data.value = &pacx->json_data[offset];
    if (node) {
      new_node->parent = (jesy_node_descriptor)(node - pacx->pool); /* node's index */

      if (HAS_CHILD(node)) {
        struct jesy_node *child = &pacx->pool[node->child];

        if (HAS_LEFT(child)) {
          struct jesy_node *last = &pacx->pool[child->left];
          last->right = (jesy_node_descriptor)(new_node - pacx->pool); /* new_node's index */
        }
        else {
          child->right = (jesy_node_descriptor)(new_node - pacx->pool); /* new_node's index */
          //new_node->left = (jesy_node_descriptor)(child - pacx->pool); /* new_node's index */
        }
        child->left = (jesy_node_descriptor)(new_node - pacx->pool); /* new_node's index */
      }
      else {
        node->child = (jesy_node_descriptor)(new_node - pacx->pool); /* new_node's index */
      }
    }
    else {
      assert(!pacx->root);
      pacx->root = new_node;
    }
  }

  return new_node;
}

static void jesy_delete_node(struct jesy_parser_context *pacx, struct jesy_node *node)
{
  struct jesy_node *iter = jesy_get_child_node(pacx, node);
  struct jesy_node *parent = NULL;
  struct jesy_node *prev = NULL;

  do {
    iter = node;
    prev = iter;
    parent = jesy_get_parent_node(pacx, iter);

    while (true) {
      while (HAS_RIGHT(iter)) {
        prev = iter;
        iter = jesy_get_right_node(pacx, iter);
      }

      if (HAS_CHILD(iter)) {
        iter = jesy_get_child_node(pacx, iter);
        parent = jesy_get_parent_node(pacx, iter);
        continue;
      }

      break;
    }
    if (prev)prev->right = -1;
    if (parent)parent->child = -1;
    if (pacx->root == iter) {
      pacx->root = NULL;
    }

    jesy_free(pacx, iter);

  } while (iter != node);
}

static struct jesy_token jesy_get_token(struct jesy_parser_context *pacx)
{
  struct jesy_token token = { 0 };

  while (true) {

    if (++pacx->offset >= pacx->json_size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = JESY_TOKEN_INVALID;
      }
      break;
    }

    char ch = pacx->json_data[pacx->offset];

    if (!token.type) {
      /* Reaching the end of the string. Deliver the last type detected. */
      if (ch == '\0') {
        token.offset = pacx->offset;
        break;
      }

      if (ch == '{') {
        UPDATE_TOKEN(token, JESY_TOKEN_OPENING_BRACKET, pacx->offset, 1);
        break;
      }

      if (ch == '}') {
        UPDATE_TOKEN(token, JESY_TOKEN_CLOSING_BRACKET, pacx->offset, 1);
        break;
      }

      if (ch == '[') {
        UPDATE_TOKEN(token, JESY_TOKEN_OPENING_BRACE, pacx->offset, 1);
        break;
      }

      if (ch == ']') {
        UPDATE_TOKEN(token, JESY_TOKEN_CLOSING_BRACE, pacx->offset, 1);
        break;
      }

      if (ch == ':') {
        UPDATE_TOKEN(token, JESY_TOKEN_COLON, pacx->offset, 1)
        break;
      }

      if (ch == ',') {
        UPDATE_TOKEN(token, JESY_TOKEN_COMMA, pacx->offset, 1)
        break;
      }

      if (ch == '\"') {
        /* Use the jesy_token_type_str offset since '\"' won't be a part of token. */
        UPDATE_TOKEN(token, JESY_TOKEN_STRING, pacx->offset + 1, 0);
        if (IS_EOF_AHEAD(pacx)) {
          UPDATE_TOKEN(token, JESY_TOKEN_INVALID, pacx->offset, 1);
          break;
        }
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JESY_TOKEN_NUMBER, pacx->offset, 1);
        /* NUMBERs do not have dedicated enclosing symbols like STRINGs.
           To prevent the tokenizer to consume too much characters, we need to
           look ahead and stop the process if the jesy_token_type_str character is one of
           EOF, ',', '}' or ']' */
        if (IS_EOF_AHEAD(pacx) ||
            (LOOK_AHEAD(pacx) == '}') ||
            (LOOK_AHEAD(pacx) == ']') ||
            (LOOK_AHEAD(pacx) == ',')) {
          break;
        }
        continue;
      }

      if (ch == '-') {
        if (!IS_EOF_AHEAD(pacx) && IS_DIGIT(LOOK_AHEAD(pacx))) {
          UPDATE_TOKEN(token, JESY_TOKEN_NUMBER, pacx->offset, 1);
          continue;
        }
        UPDATE_TOKEN(token, JESY_TOKEN_INVALID, pacx->offset, 1);
        break;
      }

      if ((ch == 't') || (ch == 'f')) {
        if ((LOOK_AHEAD(pacx) < 'a') || (LOOK_AHEAD(pacx) > 'z')) {
          UPDATE_TOKEN(token, JESY_TOKEN_INVALID, pacx->offset, 1);
          break;
        }
        UPDATE_TOKEN(token, JESY_TOKEN_BOOLEAN, pacx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JESY_TOKEN_NULL, pacx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        continue;
      }

      UPDATE_TOKEN(token, JESY_TOKEN_INVALID, pacx->offset, 1);
      break;
    }
    else if (token.type == JESY_TOKEN_STRING) {

      /* We'll not deliver '\"' symbol as a part of token. */
      if (ch == '\"') {
        break;
      }
      token.length++;
      continue;
    }
    else if (token.type == JESY_TOKEN_NUMBER) {

      if (IS_DIGIT(ch)) {
        token.length++;
        if (!IS_DIGIT(LOOK_AHEAD(pacx)) && LOOK_AHEAD(pacx) != '.') {
          break;
        }
        continue;
      }

      if (ch == '.') {
        token.length++;
        if (!IS_DIGIT(LOOK_AHEAD(pacx))) {
          token.type = JESY_TOKEN_INVALID;
          break;
        }
        continue;
      }

      if (IS_SPACE(ch)) {
        break;
      }

      token.type = JESY_TOKEN_INVALID;
      break;

    } else if (token.type == JESY_TOKEN_BOOLEAN) {
      token.length++;
      /* Look ahead to find symbols signaling the end of token. */
      if ((LOOK_AHEAD(pacx) == ',') ||
          (LOOK_AHEAD(pacx) == ']') ||
          (LOOK_AHEAD(pacx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(pacx)))) {
        /* Check against "true". Use the longer string as reference. */
        uint32_t compare_size = token.length > sizeof("true") - 1
                              ? token.length
                              : sizeof("true") - 1;
        if (memcmp("true", &pacx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        /* Check against "false". Use the longer string as reference. */
        compare_size = token.length > sizeof("false") - 1
                     ? token.length
                     : sizeof("false") - 1;
        if (memcmp("false", &pacx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        /* The token is neither true nor false. */
        token.type = JESY_TOKEN_INVALID;
        break;
      }
      continue;
    } else if (token.type == JESY_TOKEN_NULL) {
      token.length++;
      /* Look ahead to find symbols signaling the end of token. */
      if ((LOOK_AHEAD(pacx) == ',') ||
          (LOOK_AHEAD(pacx) == ']') ||
          (LOOK_AHEAD(pacx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(pacx)))) {
        /* Check against "null". Use the longer string as reference. */
        uint32_t compare_size = token.length > sizeof("null") - 1
                              ? token.length
                              : sizeof("null") - 1;
        if (memcmp("null", &pacx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        token.type = JESY_TOKEN_INVALID;
        break;
      }
      continue;
    }

    token.type = JESY_TOKEN_INVALID;
    break;
  }

  JESY_LOG_TOKEN(token.type, token.offset, token.length, &pacx->json_data[token.offset]);

  return token;
}

static enum jesy_node_type jesy_get_parent_type(struct jesy_parser_context *pacx,
                                              struct jesy_node *node)
{
  if (node) {
    return pacx->pool[node->parent].data.type;
  }
  return JESY_NONE;
}

static struct jesy_node *jesy_find_duplicated_key(struct jesy_parser_context *pacx,
              struct jesy_node *object_node, struct jesy_token *key_token)
{
  struct jesy_node *duplicated = NULL;

  if (object_node->data.type == JESY_OBJECT)
  {
    struct jesy_node *iter = object_node;
    if (HAS_CHILD(iter)) {
      iter = jesy_get_child_node(pacx, iter);
      assert(iter->data.type == JESY_KEY);
      if ((iter->data.length == key_token->length) &&
          (memcmp(iter->data.value, &pacx->json_data[key_token->offset], key_token->length) == 0)) {
        duplicated = iter;
      }
      else {
        while (HAS_RIGHT(iter)) {
          iter = jesy_get_right_node(pacx, iter);
          if ((iter->data.length == key_token->length) &&
              (memcmp(iter->data.value, &pacx->json_data[key_token->offset], key_token->length) == 0)) {
            duplicated = iter;
          }
        }
      }
    }
  }
  return duplicated;
}
/*
 *  Parser state machine steps
 *    1. Accept
 *    2. Append
 *    3. Iterate
 *    4. State transition
*/
static bool jesy_accept(struct jesy_context *ctx,
                       enum jesy_token_type token_type,
                       enum jesy_node_type node_type,
                       enum jesy_parser_state state)
{
  struct jesy_parser_context *pacx = ctx->pacx;
  if (pacx->token.type == token_type) {
#if 0
    if (pacx->iter)
    printf("\n - [%d] %s, parent:[%d], right:%d, child:%d", pacx->iter - pacx->pool, jesy_node_type_str[pacx->iter->data.type], pacx->iter->parent, pacx->iter->right, pacx->iter->child);
#endif
    if (node_type == JESY_KEY) {
#ifdef JESY_OVERWRITE_DUPLICATED_KEYS
      /* No duplicated keys in the same object are allowed.
         Only the last key:value will be reported if the keys are duplicated. */
      struct jesy_node *node = jesy_find_duplicated_key(pacx, pacx->iter, &pacx->token);
      if (node) {
        jesy_delete_node(pacx, jesy_get_child_node(pacx, node));
        pacx->iter = node;
      }
      else
#endif
      {
        struct jesy_node *new_node = NULL;
        new_node = jesy_add_node(pacx, pacx->iter, node_type, pacx->token.offset, pacx->token.length);
        if (!new_node) {
          if (!ctx->status) ctx->status = JESY_ALLOCATION_FAILED; /* Keep the first error */
          return false;
        }
        pacx->iter = new_node;
        JESY_LOG_NODE(pacx->iter - pacx->pool, pacx->iter->data.type, pacx->iter->parent, pacx->iter->right, pacx->iter->child);
      }
    }
    else if ((node_type == JESY_OBJECT)        ||
             (node_type == JESY_VALUE_STRING)  ||
             (node_type == JESY_VALUE_NUMBER)  ||
             (node_type == JESY_VALUE_BOOLEAN) ||
             (node_type == JESY_VALUE_NULL)    ||
             (node_type == JESY_ARRAY)) {
      struct jesy_node *new_node = NULL;
      new_node = jesy_add_node(pacx, pacx->iter, node_type, pacx->token.offset, pacx->token.length);
      if (!new_node) {
        if (!ctx->status) ctx->status = JESY_ALLOCATION_FAILED; /* Keep the first error */
        return false;
      }
      pacx->iter = new_node;
      JESY_LOG_NODE(pacx->iter - pacx->pool, pacx->iter->data.type, pacx->iter->parent, pacx->iter->right, pacx->iter->child);
    }

    assert(pacx->iter);
    /* Some tokens signaling an upward move through the tree.
       A ']' indicates the end of an Array and thus the end of a key:value
             pair. Go back to the parent object.
       A '}' indicates the end of an object. Go back to the parent object
       A ',' indicates the end of a value.
             if the value is a part of an array, go back parent array node.
             otherwise, go back to the parent object.
    */
    if (token_type == JESY_TOKEN_CLOSING_BRACE) {
      /* [] (empty array) is a special case that needs no iteration in the
         direction the parent node. */
      if (pacx->iter->data.type != JESY_ARRAY) {
        pacx->iter = jesy_get_parent_node_bytype(pacx, pacx->iter, JESY_ARRAY);
      }
      pacx->iter = jesy_get_structure_parent_node(pacx, pacx->iter);
    }
    else if (token_type == JESY_TOKEN_CLOSING_BRACKET) {
      /* {} (empty object)is a special case that needs no iteration in the
         direction the parent node. */
      if (pacx->iter->data.type != JESY_OBJECT) {
        pacx->iter = jesy_get_parent_node_bytype(pacx, pacx->iter, JESY_OBJECT);
      }
      pacx->iter = jesy_get_structure_parent_node(pacx, pacx->iter);
    }
    else if (token_type == JESY_TOKEN_COMMA) {
      if ((pacx->iter->data.type != JESY_OBJECT) && (pacx->iter->data.type != JESY_ARRAY)) {
        pacx->iter = jesy_get_structure_parent_node(pacx, pacx->iter);
      }
    }
    pacx->state = state;
    pacx->token = jesy_get_token(pacx);
    return true;
  }
  return false;
}

static bool jesy_expect(struct jesy_context *ctx,
                        enum jesy_token_type token_type,
                        enum jesy_node_type node_type,
                        enum jesy_parser_state state)
{
  if (jesy_accept(ctx, token_type, node_type, state)) {
    return true;
  }
  if (!ctx->status) {
    ctx->status = JESY_UNEXPECTED_TOKEN; /* Keep the first error */
#ifndef NDEBUG
  printf("\nJES.Parser error! Unexpected Token. %s \"%.*s\"",
      jesy_token_type_str[ctx->pacx->token.type], ctx->pacx->token.length,
      &ctx->pacx->json_data[ctx->pacx->token.offset]);
#endif
  }
  return false;
}

struct jesy_context* jesy_init_context(void *mem_pool, uint32_t pool_size)
{
  if (pool_size < (sizeof(struct jesy_context) +
                   sizeof(struct jesy_parser_context))) {
    return NULL;
  }

  struct jesy_context *ctx = mem_pool;
  struct jesy_parser_context *pacx = (struct jesy_parser_context *)(ctx + 1);
  ctx->pacx = pacx;
  ctx->status = 0;
  ctx->node_count = 0;

  pacx->json_data = NULL;
  pacx->json_size = 0;
  pacx->offset = (uint32_t)-1;

  pacx->pool = (struct jesy_node*)(pacx + 1);
  pacx->pool_size = pool_size - (uint32_t)(sizeof(struct jesy_context) + sizeof(struct jesy_parser_context));
  pacx->capacity = (pacx->pool_size / sizeof(struct jesy_node)) < JESY_INVALID_INDEX
                 ? (uint16_t)(pacx->pool_size / sizeof(struct jesy_node))
                 : JESY_INVALID_INDEX -1;

#ifndef NDEBUG
  printf("\nallocator capacity is %d nodes", pacx->capacity);
#endif
  pacx->allocated = 0;
  pacx->index = 0;
  pacx->iter = NULL;
  pacx->root = NULL;
  pacx->state = JESY_STATE_START;
  return ctx;
}

jesy_status jesy_parse(struct jesy_context *ctx, char *json_data, uint32_t json_length)
{
  struct jesy_parser_context *pacx = ctx->pacx;
  pacx->json_data = json_data;
  pacx->json_size = json_length;
  /* Fetch the first token to before entering the state machine. */
  pacx->token = jesy_get_token(pacx);

  do {
    if (pacx->token.type == JESY_TOKEN_EOF) break;
    //if (pacx->iter)printf("\n    State: %s, node: %s", jesy_state_str[pacx->state], jesy_node_type_str[pacx->iter->data.type]);
    switch (pacx->state) {
      /* Only an opening bracket is acceptable in this state. */
      case JESY_STATE_START:
        jesy_expect(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT, JESY_STATE_WANT_KEY);
        break;

      /* An opening parenthesis has already been found.
         A closing bracket is allowed. Otherwise, only a KEY is acceptable. */
      case JESY_STATE_WANT_KEY:
        if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACKET, JESY_NONE, JESY_STATE_STRUCTURE_END)) {
          break;
        }

        if (jesy_expect(ctx, JESY_TOKEN_STRING, JESY_KEY, JESY_STATE_WANT_VALUE)) {
          jesy_expect(ctx, JESY_TOKEN_COLON, JESY_NONE, JESY_STATE_WANT_VALUE);
        }

        break;

      case JESY_STATE_WANT_VALUE:
        if (jesy_accept(ctx, JESY_TOKEN_STRING, JESY_VALUE_STRING, JESY_STATE_PROPERTY_END)   ||
            jesy_accept(ctx, JESY_TOKEN_NUMBER, JESY_VALUE_NUMBER, JESY_STATE_PROPERTY_END)   ||
            jesy_accept(ctx, JESY_TOKEN_BOOLEAN, JESY_VALUE_BOOLEAN, JESY_STATE_PROPERTY_END) ||
            jesy_accept(ctx, JESY_TOKEN_NULL, JESY_VALUE_NULL, JESY_STATE_PROPERTY_END)       ||
            jesy_accept(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT, JESY_STATE_WANT_KEY)) {
          break;
        }

        jesy_expect(ctx, JESY_TOKEN_OPENING_BRACE, JESY_ARRAY, JESY_STATE_WANT_ARRAY);
        break;

      case JESY_STATE_WANT_ARRAY:
        if (jesy_accept(ctx, JESY_TOKEN_STRING, JESY_VALUE_STRING, JESY_STATE_VALUE_END)   ||
            jesy_accept(ctx, JESY_TOKEN_NUMBER, JESY_VALUE_NUMBER, JESY_STATE_VALUE_END)   ||
            jesy_accept(ctx, JESY_TOKEN_BOOLEAN, JESY_VALUE_BOOLEAN, JESY_STATE_VALUE_END) ||
            jesy_accept(ctx, JESY_TOKEN_NULL, JESY_VALUE_NULL, JESY_STATE_VALUE_END)       ||
            jesy_accept(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT, JESY_STATE_WANT_KEY) ||
            jesy_accept(ctx, JESY_TOKEN_OPENING_BRACE, JESY_ARRAY, JESY_STATE_WANT_ARRAY)) {
          break;
        }

        jesy_expect(ctx, JESY_TOKEN_CLOSING_BRACE, JESY_NONE, JESY_STATE_STRUCTURE_END);
        break;
      /* A Structure can be an Object or an Array.
         When a structure is closed, another closing symbol is allowed.
         Otherwise, only a separator is acceptable. */
      case JESY_STATE_PROPERTY_END:
        if (jesy_accept(ctx, JESY_TOKEN_COMMA, JESY_NONE, JESY_STATE_WANT_KEY)) {
          continue;
        }

        jesy_expect(ctx, JESY_TOKEN_CLOSING_BRACKET, JESY_NONE, JESY_STATE_STRUCTURE_END);
        break;

      /* A Structure can be an Object or an Array.
         When a structure is closed, another closing symbol is allowed.
         Otherwise, only a separator is acceptable. */
      case JESY_STATE_VALUE_END:
        if (jesy_accept(ctx, JESY_TOKEN_COMMA, JESY_NONE, JESY_STATE_WANT_ARRAY)) {
          break;
        }

        jesy_expect(ctx, JESY_TOKEN_CLOSING_BRACE, JESY_NONE, JESY_STATE_STRUCTURE_END);
        break;
      /* A Structure can be an Object or an Array.
         When a structure is closed, another closing symbol is allowed.
         Otherwise, only a separator is acceptable. */
      case JESY_STATE_STRUCTURE_END:
        if (pacx->iter->data.type == JESY_OBJECT) {
          if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACKET, JESY_NONE, JESY_STATE_STRUCTURE_END)) {
            break;
          }
          jesy_expect(ctx, JESY_TOKEN_COMMA, JESY_NONE, JESY_STATE_WANT_KEY);
        }
        else if(pacx->iter->data.type == JESY_ARRAY) {
          if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACE, JESY_NONE, JESY_STATE_STRUCTURE_END)) {
            break;
          }
          jesy_expect(ctx, JESY_TOKEN_COMMA, JESY_NONE, JESY_STATE_WANT_VALUE);
        }
        break;

      default:
        assert(0);
        break;
    }

  } while ((pacx->iter) && (ctx->status == 0));

  if (ctx->status == 0) {
    if (pacx->token.type != JESY_TOKEN_EOF) {
      printf("\nJES.Parser error! Expected EOF, but got: %s", jesy_token_type_str[pacx->token.type]);
      ctx->status = 100;
    }
    if (pacx->iter) {
      printf("\nJES.Parser error! Expected data but got EOF.");
      ctx->status = 101;
    }

  }
  ctx->pacx->iter = ctx->pacx->root;
  ctx->node_count = pacx->allocated;
  ctx->required_mem = pacx->allocated * sizeof(struct jesy_node);
  return ctx->status;
}

jesy_status jesy_serialize(struct jesy_context *ctx, char *json_data, uint32_t length)
{
  struct jesy_node *iter = ctx->pacx->root;
  char *dst = json_data;
  jesy_status result = 0;

  while (iter) {
    switch (iter->data.type) {
      case JESY_OBJECT:
        *dst++ = '{';
        break;
      case JESY_KEY:
        *dst++ = '"';
        dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
        *dst++ = '"';
        *dst++ = ':';
        break;
      case JESY_VALUE_STRING:
        *dst++ = '"';
        dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
        *dst++ = '"';
        break;
      case JESY_VALUE_NUMBER:
      case JESY_VALUE_BOOLEAN:
      case JESY_VALUE_NULL:
        dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
        break;
      case JESY_ARRAY:
        *dst++ = '[';
        break;
      default:
      case JESY_NONE:
        assert(0);
        return 1;
    }

    if (HAS_CHILD(iter)) {
      iter = jesy_get_child_node(ctx->pacx, iter);
      continue;
    }

    if (iter->data.type == JESY_OBJECT) {
      *dst++ = '}';
    }

    else if (iter->data.type == JESY_ARRAY) {
      *dst++ = ']';
    }

    if (HAS_RIGHT(iter)) {
      iter = jesy_get_right_node(ctx->pacx, iter);
      *dst++ = ',';
      continue;
    }

     while ((iter = jesy_get_parent_node(ctx->pacx, iter))) {
      if (iter->data.type == JESY_OBJECT) {
        *dst++ = '}';
      }
      else if (iter->data.type == JESY_ARRAY) {
        *dst++ = ']';
      }
      if (HAS_RIGHT(iter)) {
        iter = jesy_get_right_node(ctx->pacx, iter);
        *dst++ = ',';
        break;
      }
    }
  }
  *dst = '\0';
  return result;
}

struct jessy_element jesy_get_root(struct jesy_context *ctx)
{
  if (ctx) {
    ctx->pacx->iter = ctx->pacx->root;
    return ctx->pacx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

struct jessy_element jesy_get_parent(struct jesy_context *ctx)
{
  if ((ctx) && HAS_PARENT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->parent];
    return ctx->pacx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

struct jessy_element jesy_get_child(struct jesy_context *ctx)
{
  if ((ctx) && HAS_CHILD(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->child];
    return ctx->pacx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

struct jessy_element jesy_get_next(struct jesy_context *ctx)
{
  if ((ctx) && HAS_RIGHT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->right];
    return ctx->pacx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

void jesy_reset_iterator(struct jesy_context *ctx)
{
  ctx->pacx->iter = ctx->pacx->root;
}

bool jesy_find(struct jesy_context *ctx, char *key)
{
  bool result = false;
  struct jesy_node *iter = ctx->pacx->iter;
  uint16_t key_length = (uint16_t)strnlen(key, 0xFFFF);
  if ((key_length == 0) || (key_length == 0xFFFF)) {
    return false;
  }

  if (iter->data.type != JESY_OBJECT) {
    if (!(iter = jesy_get_parent_node_bytype(ctx->pacx, iter, JESY_OBJECT))) {
      return false;
    }
  }

  iter = jesy_get_child_node(ctx->pacx, iter);
  assert(iter);
  assert(iter->data.type == JESY_KEY);

  while (iter) {
    if ((iter->data.length == key_length) &&
        (memcmp(iter->data.value, key, key_length) == 0)) {
      ctx->pacx->iter = iter;
      result = true;
      break;
    }
    iter = jesy_get_right_node(ctx->pacx, iter);
  }
  return result;
}

bool jesy_has(struct jesy_context *ctx, char *key)
{
  bool result = false;
  struct jesy_node *iter = ctx->pacx->iter;
  uint16_t key_length = (uint16_t)strnlen(key, 0xFFFF);
  if ((key_length == 0) || (key_length == 0xFFFF)) {
    return false;
  }

  if (iter->data.type != JESY_OBJECT) {
    if (!(iter = jesy_get_parent_node_bytype(ctx->pacx, iter, JESY_OBJECT))) {
      return false;
    }
  }

  iter = jesy_get_child_node(ctx->pacx, iter);
  assert(iter);
  assert(iter->data.type == JESY_KEY);

  while (iter) {
    if ((iter->data.length == key_length) &&
        (memcmp(iter->data.value, key, key_length) == 0)) {
      result = true;
      break;
    }
    iter = jesy_get_right_node(ctx->pacx, iter);
  }
  return result;
}


enum jesy_node_type jesy_get_type(struct jesy_context *ctx, char *key)
{
  enum jesy_node_type result = JESY_NONE;
  struct jesy_node *iter = ctx->pacx->iter;
  uint16_t key_length = (uint16_t)strnlen(key, 0xFFFF);
  if ((key_length == 0) || (key_length == 0xFFFF)) {
    return false;
  }

  if (iter->data.type != JESY_OBJECT) {
    if (!(iter = jesy_get_parent_node_bytype(ctx->pacx, iter, JESY_OBJECT))) {
      return false;
    }
  }

  iter = jesy_get_child_node(ctx->pacx, iter);
  assert(iter);
  assert(iter->data.type == JESY_KEY);

  while (iter) {
    if ((iter->data.length == key_length) &&
        (memcmp(iter->data.value, key, key_length) == 0)) {
      if (HAS_CHILD(iter)) {
          result = jesy_get_child_node(ctx->pacx, iter)->data.type;
          break;
      }
    }
    iter = jesy_get_right_node(ctx->pacx, iter);
  }
  return result;
}

bool jesy_set(struct jesy_context *ctx, char *key, char *value, uint16_t length)
{




}