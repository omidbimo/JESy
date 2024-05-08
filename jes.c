
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "jes.h"
#include "jes_util.h"

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

#define HAS_CHILD(node_ptr) (node_ptr->child > -1)
#define HAS_LEFT(node_ptr) (node_ptr->left > -1)
#define HAS_RIGHT(node_ptr) (node_ptr->right > -1)
#define HAS_PARENT(node_ptr) (node_ptr->parent > -1)

#define GET_CONTEXT(pacx_)
#define GET_PARSER_CONTEXT(tnode_)
#define GET_TREE_NODE(node_)

enum jes_token_type {
  JES_TOKEN_EOF = 0,
  JES_TOKEN_OPENING_BRACKET,
  JES_TOKEN_CLOSING_BRACKET,
  JES_TOKEN_OPENING_BRACE,
  JES_TOKEN_CLOSING_BRACE,
  JES_TOKEN_STRING,
  JES_TOKEN_NUMBER,
  JES_TOKEN_BOOLEAN,
  JES_TOKEN_NULL,
  JES_TOKEN_COLON,
  JES_TOKEN_COMMA,
  JES_TOKEN_ESC,
  JES_TOKEN_INVALID,
};

enum jes_parser_state {
  JES_STATE_START = 0,
  JES_STATE_WANT_KEY,
  JES_STATE_WANT_VALUE,
  JES_STATE_WANT_ARRAY,
  JES_STATE_STRUCTURE_END,
};

typedef int16_t jes_node_descriptor;


struct jes_node {
  jes_node_descriptor parent;
  jes_node_descriptor left;
  jes_node_descriptor right;
  jes_node_descriptor child;
  struct jes_json_element data;
};

struct jes_tree_free_node {
  struct jes_tree_free_node *next;
};

struct jes_token {
  enum jes_token_type type;
  uint16_t length;
  uint32_t offset;
};

struct jes_parser_context {
  char     *json_data;
  uint32_t  json_size;
  uint32_t  offset;

  uint16_t  capacity;
  uint16_t  allocated;
  uint16_t  index;

  enum jes_parser_state state;
  uint32_t  pool_size;
  void      *node_pool;

  struct jes_token token;

  struct jes_node *iter;
  struct jes_node *root;
  struct jes_node *pool;

  struct jes_tree_free_node *free;
};

static struct jes_node *jes_find_duplicated_key(struct jes_parser_context *pacx,
              struct jes_node *object_node, struct jes_token *key_token);

static struct jes_node* jes_allocate(struct jes_parser_context *pacx)
{
  struct jes_node *new_node = NULL;

  if (pacx->allocated < pacx->capacity) {
    if (pacx->free) {
      /* Pop the first node from free list */
      new_node = (struct jes_node*)pacx->free;
      pacx->free = pacx->free->next;
    }
    else {
      assert(pacx->index < pacx->capacity);
      new_node = &pacx->pool[pacx->index];
      pacx->index++;
    }
    memset(new_node, 0, sizeof(*new_node));
    new_node->parent = -1;
    new_node->child = -1;
    new_node->right = -1;
    pacx->allocated++;
  }

  return new_node;
}

static void jes_free(struct jes_parser_context *pacx, struct jes_node *node)
{
  struct jes_tree_free_node *free_node = (struct jes_tree_free_node*)node;

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

static struct jes_node* jes_get_parent_node(struct jes_parser_context *pacx,
                                                 struct jes_node *node)
{
  /* TODO: add checking */
  if (node && (HAS_PARENT(node))) {
    return &pacx->pool[node->parent];
  }

  return NULL;
}

static struct jes_node* jes_get_parent_node_bytype(struct jes_parser_context *pacx,
                                                        struct jes_node *node,
                                                        enum jes_node_type type)
{
  struct jes_node *parent = NULL;
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

static struct jes_node* jes_get_structure_parent_node(struct jes_parser_context *pacx,
                                                      struct jes_node *node)
{
  struct jes_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &pacx->pool[node->parent];
    if ((node->data.type == JES_OBJECT) || (node->data.type == JES_ARRAY)) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jes_node* jes_get_child_node(struct jes_parser_context *pacx,
                                                struct jes_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_CHILD(node)) {
    return &pacx->pool[node->child];
  }

  return NULL;
}

static struct jes_node* jes_get_right_node(struct jes_parser_context *pacx,
                                                struct jes_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_RIGHT(node)) {
    return &pacx->pool[node->right];
  }

  return NULL;
}

static struct jes_node* jes_add_node(struct jes_parser_context *pacx,
                                          struct jes_node *node,
                                          uint16_t type,
                                          uint32_t offset,
                                          uint16_t length)
{
  struct jes_node *new_node = jes_allocate(pacx);

  if (new_node) {
    new_node->data.type = type;
    new_node->data.length = length;
    new_node->data.value = &pacx->json_data[offset];
    if (node) {
      new_node->parent = (jes_node_descriptor)(node - pacx->pool); /* node's index */

      if (HAS_CHILD(node)) {
        struct jes_node *child = &pacx->pool[node->child];
        while (HAS_RIGHT(child)) {
          child = &pacx->pool[child->right];
        }
        child->right = (jes_node_descriptor)(new_node - pacx->pool); /* new_node's index */
        new_node->left = (jes_node_descriptor)(child - pacx->pool);
      }
      else {
        node->child = (jes_node_descriptor)(new_node - pacx->pool); /* new_node's index */
      }
    }
    else {
      assert(!pacx->root);
      pacx->root = new_node;
    }
  }

  return new_node;
}

static void jes_delete_node(struct jes_parser_context *pacx, struct jes_node *node)
{
  struct jes_node *iter = jes_get_child_node(pacx, node);
  struct jes_node *parent = NULL;
  struct jes_node *prev = NULL;

  do {
    iter = node;
    prev = iter;
    parent = jes_get_parent_node(pacx, iter);

    while (true) {
      while (HAS_RIGHT(iter)) {
        prev = iter;
        iter = jes_get_right_node(pacx, iter);
      }

      if (HAS_CHILD(iter)) {
        iter = jes_get_child_node(pacx, iter);
        parent = jes_get_parent_node(pacx, iter);
        continue;
      }

      break;
    }
    if (prev)prev->right = -1;
    if (parent)parent->child = -1;
    if (pacx->root == iter) {
      pacx->root = NULL;
    }

    jes_free(pacx, iter);

  } while (iter != node);
}

static struct jes_token jes_get_token(struct jes_parser_context *pacx)
{
  struct jes_token token = { 0 };

  while (true) {

    if (++pacx->offset >= pacx->json_size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = JES_TOKEN_INVALID;
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
        UPDATE_TOKEN(token, JES_TOKEN_OPENING_BRACKET, pacx->offset, 1);
        break;
      }

      if (ch == '}') {
        UPDATE_TOKEN(token, JES_TOKEN_CLOSING_BRACKET, pacx->offset, 1);
        break;
      }

      if (ch == '[') {
        UPDATE_TOKEN(token, JES_TOKEN_OPENING_BRACE, pacx->offset, 1);
        break;
      }

      if (ch == ']') {
        UPDATE_TOKEN(token, JES_TOKEN_CLOSING_BRACE, pacx->offset, 1);
        break;
      }

      if (ch == ':') {
        UPDATE_TOKEN(token, JES_TOKEN_COLON, pacx->offset, 1)
        break;
      }

      if (ch == ',') {
        UPDATE_TOKEN(token, JES_TOKEN_COMMA, pacx->offset, 1)
        break;
      }

      if (ch == '\"') {
        /* Use the jes_token_type_str offset since '\"' won't be a part of token. */
        UPDATE_TOKEN(token, JES_TOKEN_STRING, pacx->offset + 1, 0);
        if (IS_EOF_AHEAD(pacx)) {
          UPDATE_TOKEN(token, JES_TOKEN_INVALID, pacx->offset, 1);
          break;
        }
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, pacx->offset, 1);
        /* NUMBERs do not have dedicated enclosing symbols like STRINGs.
           To prevent the tokenizer to consume too much characters, we need to
           look ahead and stop the process if the jes_token_type_str character is one of
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
          UPDATE_TOKEN(token, JES_TOKEN_NUMBER, pacx->offset, 1);
          continue;
        }
        UPDATE_TOKEN(token, JES_TOKEN_INVALID, pacx->offset, 1);
        break;
      }

      if ((ch == 't') || (ch == 'f')) {
        if ((LOOK_AHEAD(pacx) < 'a') || (LOOK_AHEAD(pacx) > 'z')) {
          UPDATE_TOKEN(token, JES_TOKEN_INVALID, pacx->offset, 1);
          break;
        }
        UPDATE_TOKEN(token, JES_TOKEN_BOOLEAN, pacx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JES_TOKEN_NULL, pacx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        continue;
      }

      UPDATE_TOKEN(token, JES_TOKEN_INVALID, pacx->offset, 1);
      break;
    }
    else if (token.type == JES_TOKEN_STRING) {

      /* We'll not deliver '\"' symbol as a part of token. */
      if (ch == '\"') {
        break;
      }
      token.length++;
      continue;
    }
    else if (token.type == JES_TOKEN_NUMBER) {

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
          token.type = JES_TOKEN_INVALID;
          break;
        }
        continue;
      }

      if (IS_SPACE(ch)) {
        break;
      }

      token.type = JES_TOKEN_INVALID;
      break;

    } else if (token.type == JES_TOKEN_BOOLEAN) {
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
        token.type = JES_TOKEN_INVALID;
        break;
      }
      continue;
    } else if (token.type == JES_TOKEN_NULL) {
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
        token.type = JES_TOKEN_INVALID;
        break;
      }
      continue;
    }

    token.type = JES_TOKEN_INVALID;
    break;
  }

  JES_LOG_TOKEN(token.type, token.offset, token.length, &pacx->json_data[token.offset]);

  return token;
}

static struct jes_node *jes_find_duplicated_key(struct jes_parser_context *pacx,
              struct jes_node *object_node, struct jes_token *key_token)
{
  struct jes_node *duplicated = NULL;

  if (object_node->data.type == JES_OBJECT)
  {
    struct jes_node *iter = object_node;
    if (HAS_CHILD(iter)) {
      iter = jes_get_child_node(pacx, iter);
      assert(iter->data.type == JES_KEY);
      if ((iter->data.length == key_token->length) &&
          (memcmp(iter->data.value, &pacx->json_data[key_token->offset], key_token->length) == 0)) {
        duplicated = iter;
      }
      else {
        while (HAS_RIGHT(iter)) {
          iter = jes_get_right_node(pacx, iter);
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

static bool jes_accept(struct jes_parser_context *pacx,
                       enum jes_token_type token_type,
                       enum jes_node_type node_type,
                       enum jes_parser_state state)
{
  if (pacx->token.type == token_type) {
#if 0
    if (pacx->iter)
    printf("\n - [%d] %s, parent:[%d], right:%d, child:%d", pacx->iter - pacx->pool, jes_node_type_str[pacx->iter->data.type], pacx->iter->parent, pacx->iter->right, pacx->iter->child);
#endif
    switch (node_type) {
      case JES_KEY:            /* Fall through intended. */
        {
          /* If there is already a key with the same name, we'll overwrite it. */
          struct jes_node *node = jes_find_duplicated_key(pacx, pacx->iter, &pacx->token);
          if (node) {
            jes_delete_node(pacx, jes_get_child_node(pacx, node));
            pacx->iter = node;
            break;
          }
        }
      case JES_OBJECT:         /* Fall through intended. */
      case JES_VALUE_STRING:   /* Fall through intended. */
      case JES_VALUE_NUMBER:   /* Fall through intended. */
      case JES_VALUE_BOOLEAN:  /* Fall through intended. */
      case JES_VALUE_NULL:     /* Fall through intended. */
      case JES_ARRAY:
        pacx->iter = jes_add_node(pacx, pacx->iter, node_type, pacx->token.offset, pacx->token.length);
        if (!pacx->iter) {
          /* Allocation is failed. */
          return false;
        }

        JES_LOG_NODE(pacx->iter - pacx->pool, pacx->iter->data.type, pacx->iter->parent, pacx->iter->right, pacx->iter->child);
        break;

      case JES_NONE:
        /* Some tokens signaling an upward move through the tree.
           A ']' indicates the end of an Array and thus the end of a key:value
                 pair. Go back to the parent object.
           A '}' indicates the end of an object. Go back to the parent object
           A ',' indicates the end of a value.
                 if the value is a part of an array, go back parent array node.
                 otherwise, go back to the parent object.
        */
        if (token_type == JES_TOKEN_CLOSING_BRACE) {
          /* End of an empty Array. No upward move to the parent node. */
          if ((pacx->iter->data.type == JES_ARRAY) && (!HAS_CHILD(pacx->iter))) {
            break;
          }
          pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_ARRAY);
        }
        else if (token_type == JES_TOKEN_CLOSING_BRACKET) {
          /* End of an empty Object. No upward move to the parent node. */
          if ((pacx->iter->data.type == JES_OBJECT) && (!HAS_CHILD(pacx->iter))) {
            break;
          }
          pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_OBJECT);
        }
        else if (token_type == JES_TOKEN_COMMA) {
          pacx->iter = jes_get_structure_parent_node(pacx, pacx->iter);
        }

        assert(pacx->iter);
        break;
      default:

        break;
    }

    pacx->state = state;
    pacx->token = jes_get_token(pacx);
    return true;
  }
  return false;
}

static bool jes_expect(struct jes_parser_context *pacx,
                       enum jes_token_type token_type,
                       enum jes_node_type node_type,
                       enum jes_parser_state state)
{
  if (jes_accept(pacx, token_type, node_type, state)) {
    return true;
  }
#ifndef NDEBUG
  printf("\nJES::Parser error! Unexpected Token. expected: %s, got: %s \"%.*s\"",
      jes_token_type_str[token_type], jes_token_type_str[pacx->token.type], pacx->token.length,
      &pacx->json_data[pacx->token.offset]);
#endif
  return false;
}

struct jes_context* jes_init_context(void *mem_pool, uint32_t pool_size)
{
  if (pool_size < (sizeof(struct jes_context) +
                   sizeof(struct jes_parser_context))) {
    return NULL;
  }

  struct jes_context *ctx = mem_pool;
  struct jes_parser_context *pacx = (struct jes_parser_context *)(ctx + 1);
  ctx->pacx = pacx;
  ctx->status = 0;
  ctx->node_count = 0;

  pacx->json_data = NULL;
  pacx->json_size = 0;
  pacx->offset = (uint32_t)-1;

  pacx->pool = (struct jes_node*)(pacx + 1);
  pacx->pool_size = pool_size - (uint32_t)(sizeof(struct jes_context) + sizeof(struct jes_parser_context));
  pacx->capacity = (uint16_t)(pacx->pool_size / sizeof(struct jes_node));
#ifndef NDEBUG
  printf("\nallocator capacity is %d nodes", pacx->capacity);
#endif
  pacx->allocated = 0;
  pacx->index = 0;
  pacx->iter = NULL;
  pacx->root = NULL;
  pacx->state = JES_STATE_START;
  return ctx;
}

jes_status jes_parse(struct jes_context *ctx, char *json_data, uint32_t json_length)
{
  jes_status result = 0;
  struct jes_parser_context *pacx = ctx->pacx;
  pacx->json_data = json_data;
  pacx->json_size = json_length;
  /* Fetch the first token to before entering the state machine. */
  pacx->token = jes_get_token(pacx);

  while (pacx->token.type != JES_TOKEN_EOF) {

    switch (pacx->state) {
      /* Only an opening bracket is acceptable in this state. */
      case JES_STATE_START:
        if (jes_expect(pacx, JES_TOKEN_OPENING_BRACKET, JES_OBJECT, JES_STATE_WANT_KEY)) {
          continue;
        }

        ctx->status = 1;
        break;

      /* An opening parenthesis has already been found.
         A closing bracket is allowed. Otherwise, only a KEY is acceptable. */
      case JES_STATE_WANT_KEY:
        if (jes_accept(pacx, JES_TOKEN_CLOSING_BRACKET, JES_NONE, JES_STATE_STRUCTURE_END)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOKEN_STRING, JES_KEY, JES_STATE_WANT_VALUE) &&
            jes_expect(pacx, JES_TOKEN_COLON, JES_NONE, JES_STATE_WANT_VALUE)) {
          continue;
        }

        ctx->status = 1;
        break;

      /* A Structure can be an Object or an Array.
         When a structure is closed, another closing symbol is allowed.
         Otherwise, only a separator is acceptable. */
      case JES_STATE_STRUCTURE_END:
        if (jes_accept(pacx, JES_TOKEN_CLOSING_BRACKET, JES_NONE, JES_STATE_STRUCTURE_END) ||
            jes_accept(pacx, JES_TOKEN_CLOSING_BRACE, JES_NONE, JES_STATE_STRUCTURE_END)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOKEN_COMMA, JES_NONE, JES_STATE_WANT_KEY)) {
          continue;
        }

        ctx->status = 1;
        break;

      case JES_STATE_WANT_VALUE:
        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACKET, JES_OBJECT, JES_STATE_WANT_KEY)) {
          continue;
        }

        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACE, JES_ARRAY, JES_STATE_WANT_ARRAY)) {
          continue;
        }

        if (jes_accept(pacx, JES_TOKEN_STRING, JES_VALUE_STRING, JES_STATE_STRUCTURE_END) ||
            jes_accept(pacx, JES_TOKEN_NUMBER, JES_VALUE_NUMBER, JES_STATE_STRUCTURE_END)   ||
            jes_accept(pacx, JES_TOKEN_BOOLEAN, JES_VALUE_BOOLEAN, JES_STATE_STRUCTURE_END) ||
            jes_accept(pacx, JES_TOKEN_NULL, JES_VALUE_NULL, JES_STATE_STRUCTURE_END)) {
          continue;
        }

        ctx->status = 1;
        break;

      case JES_STATE_WANT_ARRAY:
        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACKET, JES_OBJECT, JES_STATE_WANT_KEY)) {
          continue;
        }

        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACE, JES_ARRAY, JES_STATE_WANT_ARRAY)) {
          continue;
        }

        if (jes_accept(pacx, JES_TOKEN_STRING, JES_VALUE_STRING, JES_STATE_WANT_ARRAY) ||
            jes_accept(pacx, JES_TOKEN_NUMBER, JES_VALUE_NUMBER, JES_STATE_WANT_ARRAY)   ||
            jes_accept(pacx, JES_TOKEN_BOOLEAN, JES_VALUE_BOOLEAN, JES_STATE_WANT_ARRAY) ||
            jes_accept(pacx, JES_TOKEN_NULL, JES_VALUE_NULL, JES_STATE_WANT_ARRAY)) {
          if (jes_accept(pacx, JES_TOKEN_COMMA, JES_NONE, JES_STATE_WANT_ARRAY)) {
            continue;
          }

          if (jes_expect(pacx, JES_TOKEN_CLOSING_BRACE, JES_NONE, JES_STATE_STRUCTURE_END)) {
            continue;
          }
        }

        ctx->status = 1;
        break;

      default:
        assert(0);
        break;
    }

    break;
  }

  ctx->node_count = pacx->allocated;
  ctx->required_mem = pacx->allocated * sizeof(struct jes_node);
  return ctx->status;
}

jes_status jes_serialize(struct jes_context *ctx, char *json_data, uint32_t length)
{
  struct jes_node *iter = ctx->pacx->root;
  char *dst = json_data;
  jes_status result = 0;

  while (iter) {
    switch (iter->data.type) {
      case JES_OBJECT:
        *dst++ = '{';
        break;
      case JES_KEY:
        *dst++ = '"';
        dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
        *dst++ = '"';
        *dst++ = ':';
        break;
      case JES_VALUE_STRING:
        *dst++ = '"';
        dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
        *dst++ = '"';
        break;
      case JES_VALUE_NUMBER:
      case JES_VALUE_BOOLEAN:
      case JES_VALUE_NULL:
        dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
        break;
      case JES_ARRAY:
        *dst++ = '[';
        break;
      default:
      case JES_NONE:
        assert(0);
        return 1;
    }

    if (HAS_CHILD(iter)) {
      iter = jes_get_child_node(ctx->pacx, iter);
      continue;
    }

    if (iter->data.type == JES_OBJECT) {
      *dst++ = '}';
    }

    else if (iter->data.type == JES_ARRAY) {
      *dst++ = ']';
    }

    if (HAS_RIGHT(iter)) {
      iter = jes_get_right_node(ctx->pacx, iter);
      *dst++ = ',';
      continue;
    }

     while ((iter = jes_get_parent_node(ctx->pacx, iter))) {
      if (iter->data.type == JES_OBJECT) {
        *dst++ = '}';
      }
      else if (iter->data.type == JES_ARRAY) {
        *dst++ = ']';
      }
      if (HAS_RIGHT(iter)) {
        iter = jes_get_right_node(ctx->pacx, iter);
        *dst++ = ',';
        break;
      }
    }
  }
  *dst = '\0';
  return result;
}

struct jes_json_element jes_get_root(struct jes_context *ctx)
{
  if (ctx) {
    ctx->pacx->iter = ctx->pacx->root;
    return ctx->pacx->iter->data;
  }
  return (struct jes_json_element){ 0 };
}

struct jes_json_element jes_get_parent(struct jes_context *ctx)
{
  if ((ctx) && HAS_PARENT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->parent];
    return ctx->pacx->iter->data;
  }
  return (struct jes_json_element){ 0 };
}

struct jes_json_element jes_get_child(struct jes_context *ctx)
{
  if ((ctx) && HAS_CHILD(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->child];
    return ctx->pacx->iter->data;
  }
  return (struct jes_json_element){ 0 };
}

struct jes_json_element jes_get_next(struct jes_context *ctx)
{
  if ((ctx) && HAS_RIGHT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->right];
    return ctx->pacx->iter->data;
  }
  return (struct jes_json_element){ 0 };
}

void jes_reset_iterator(struct jes_context *ctx)
{
  ctx->pacx->iter = ctx->pacx->root;
}


