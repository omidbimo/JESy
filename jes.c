
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>
#include "jes.h"

#define LOG

#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.size = size_;

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


static char jes_token_type_str[][20] = {
  "EOF          ",
  "OPENING_BRACKET ",
  "CLOSING_BRACKET",
  "OPENING_BRACE   ",
  "CLOSING_BRACE  ",
  "STRING       ",
  "NUMBER       ",
  "BOOLEAN      ",
  "NULL         ",
  "COLON        ",
  "COMMA        ",
  "ESC          ",
  "INVALID      ",
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

static char jes_state_str[][20] = {
  "STATE_START",
  "STATE_WANT_KEY",
  "STATE_WANT_VALUE",
  "STATE_WANT_ARRAY",
  "STATE_COMPOSITE_END",
};
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
  JES_STATE_COMPOSITE_END,
};

typedef int16_t jes_node_descriptor;

struct jes_tree_node {
  struct jes_node data;
  jes_node_descriptor parent;
  jes_node_descriptor left;
  jes_node_descriptor right;
  jes_node_descriptor child;
};

struct jes_tree_free_node {
  struct jes_tree_free_node *next;
};

struct jes_token {
  enum jes_token_type type;
  uint32_t offset;
  uint32_t size;
};

struct jes_parser_context {
  uint8_t   *json_data;
  uint32_t  json_size;
  int32_t   offset;
  uint32_t  pool_size;
  uint16_t  capacity;
  uint16_t  allocated;
  int16_t   index;
  enum jes_parser_state state;
  struct jes_token token;
  struct jes_tree_node *iter;
  struct jes_tree_node *root;
  struct jes_tree_node *pool;
  struct jes_tree_free_node *free;
};

static struct jes_tree_node *jes_find_duplicated_key(struct jes_parser_context *pacx,
              struct jes_tree_node *object_node, struct jes_token *key_token);

static void jes_log(struct jes_parser_context *ctx, const struct jes_token *token)
{
  printf("\n    JES::Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token->offset, token->size, jes_token_type_str[token->type],
          token->size, &ctx->json_data[token->offset]);
}

static struct jes_tree_node* jes_allocate(struct jes_parser_context *pacx)
{
  struct jes_tree_node *new_node = NULL;

  if (pacx->allocated < pacx->capacity) {
    if (pacx->free) {
      /* Pop the first node from free list */
      new_node = (struct jes_tree_node*)pacx->free;
      pacx->free = pacx->free->next;
    }
    else {
      assert(pacx->index < pacx->capacity);
      new_node = &pacx->pool[pacx->index];
      pacx->index++;
    }
    memset(new_node, 0, sizeof(*new_node));
    //new_node->self = new_node - pacx->pool;
    new_node->parent = -1;
    new_node->child = -1;
    new_node->right = -1;
    pacx->allocated++;
  }

  return new_node;
}

static void jes_free(struct jes_parser_context *pacx, struct jes_tree_node *node)
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

static struct jes_tree_node* jes_get_parent_node(struct jes_parser_context *pacx,
                                                 struct jes_tree_node *node)
{
  /* TODO: add checking */
  if (node && (HAS_PARENT(node))) {
    return &pacx->pool[node->parent];
  }

  return NULL;
}

static struct jes_tree_node* jes_get_parent_node_bytype(struct jes_parser_context *pacx,
                                                        struct jes_tree_node *node,
                                                        enum jes_node_type type)
{
  struct jes_tree_node *parent = NULL;
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

static struct jes_tree_node* jes_get_child_node(struct jes_parser_context *pacx,
                                                struct jes_tree_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_CHILD(node)) {
    return &pacx->pool[node->child];
  }

  return NULL;
}

static struct jes_tree_node* jes_get_right_node(struct jes_parser_context *pacx,
                                                struct jes_tree_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_RIGHT(node)) {
    return &pacx->pool[node->right];
  }

  return NULL;
}

static struct jes_tree_node* jes_get_left_node(struct jes_parser_context *pacx,
                                               struct jes_tree_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_LEFT(node)) {
    return &pacx->pool[node->left];
  }

  return NULL;
}

static enum jes_node_type jes_get_parent_type(struct jes_parser_context *pacx,
                                              struct jes_tree_node *node)
{
  if (node) {
    return pacx->pool[node->parent].data.type;
  }
  return JES_NONE;
}

static struct jes_tree_node* jes_add_node(struct jes_parser_context *pacx,
                                          struct jes_tree_node *node,
                                          uint16_t type,
                                          uint32_t offset,
                                          uint16_t size)
{
  struct jes_tree_node *new_node = jes_allocate(pacx);

  if (new_node) {
    new_node->data.type = type;
    new_node->data.size = size;
    new_node->data.start = &pacx->json_data[offset];
    if (node) {
      new_node->parent = node - pacx->pool; /* node's index */

      if (HAS_CHILD(node)) {
        struct jes_tree_node *child = &pacx->pool[node->child];
        while (HAS_RIGHT(child)) {
          child = &pacx->pool[child->right];
        }
        child->right = new_node - pacx->pool; /* new_node's index */
        new_node->left = child - pacx->pool;
      }
      else {
        node->child = new_node - pacx->pool; /* new_node's index */
      }
    }
    else {
      assert(!pacx->root);
      pacx->root = new_node;
    }
  }

  return new_node;
}

static void jes_delete_node(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  struct jes_tree_node *iter = jes_get_child_node(pacx, node);
  struct jes_tree_node *parent = NULL;
  struct jes_tree_node *prev = NULL;

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

      if (ch == '\\') {
        if (LOOK_AHEAD(pacx) != 'n') {
          token.type = JES_TOKEN_INVALID;
          break;
        }
      }

      token.size++;
      continue;
    }
    else if (token.type == JES_TOKEN_NUMBER) {

      if (IS_DIGIT(ch)) {
        token.size++;
        if (!IS_DIGIT(LOOK_AHEAD(pacx)) && LOOK_AHEAD(pacx) != '.') {
          break;
        }
        continue;
      }

      if (ch == '.') {
        token.size++;
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
      token.size++;
      /* Look ahead to find symbols signaling the end of token. */
      if ((LOOK_AHEAD(pacx) == ',') ||
          (LOOK_AHEAD(pacx) == ']') ||
          (LOOK_AHEAD(pacx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(pacx)))) {
        /* Check against "true". Use the longer string as reference. */
        uint32_t compare_size = token.size > sizeof("true") - 1
                              ? token.size
                              : sizeof("true") - 1;
        if (memcmp("true", &pacx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        /* Check against "false". Use the longer string as reference. */
        compare_size = token.size > sizeof("false") - 1
                     ? token.size
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
      token.size++;
      /* Look ahead to find symbols signaling the end of token. */
      if ((LOOK_AHEAD(pacx) == ',') ||
          (LOOK_AHEAD(pacx) == ']') ||
          (LOOK_AHEAD(pacx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(pacx)))) {
        /* Check against "null". Use the longer string as reference. */
        uint32_t compare_size = token.size > sizeof("null") - 1
                              ? token.size
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

#ifdef LOG
    jes_log(pacx, &token);
#endif
  return token;
}

static struct jes_tree_node *jes_find_duplicated_key(struct jes_parser_context *pacx,
              struct jes_tree_node *object_node, struct jes_token *key_token)
{
  struct jes_tree_node *duplicated = NULL;

  if (object_node->data.type == JES_OBJECT)
  {
    struct jes_tree_node *iter = object_node;
    if (HAS_CHILD(iter)) {
      iter = jes_get_child_node(pacx, iter);
      assert(iter->data.type == JES_KEY);
      if ((iter->data.size == key_token->size) &&
          (memcmp(iter->data.start, &pacx->json_data[key_token->offset], key_token->size) == 0)) {
        duplicated = iter;
      }
      else {
        while (HAS_RIGHT(iter)) {
          iter = jes_get_right_node(pacx, iter);
          if ((iter->data.size == key_token->size) &&
              (memcmp(iter->data.start, &pacx->json_data[key_token->offset], key_token->size) == 0)) {
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
    switch (node_type) {
      case JES_KEY:            /* Fall through intended. */
      {
        /* If there is already a key with the same name, we'll overwrite it. */
        struct jes_tree_node *node = jes_find_duplicated_key(pacx, pacx->iter, &pacx->token);
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
        pacx->iter = jes_add_node(pacx, pacx->iter, node_type, pacx->token.offset, pacx->token.size);
        if (!pacx->iter) {
          printf("\nJES Error! Allocation failed.");
          return false;
        }

#ifdef LOG
        printf("\n    JES::Node: %s, %s >>> %s", jes_node_type_str[node_type], jes_state_str[pacx->state], jes_state_str[state]);
#endif
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
          pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_OBJECT);
        }
        else if (token_type == JES_TOKEN_CLOSING_BRACKET) {
          if (pacx->iter->data.type != JES_OBJECT) {
            pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_OBJECT);
          }
          pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_OBJECT);
        }
        else if ((token_type == JES_TOKEN_COMMA) &&
                 (pacx->iter->data.type == JES_VALUE_STRING  ||
                  pacx->iter->data.type == JES_VALUE_NUMBER  ||
                  pacx->iter->data.type == JES_VALUE_BOOLEAN ||
                  pacx->iter->data.type == JES_VALUE_NULL)) {
          if (jes_get_parent_type(pacx, pacx->iter) == JES_ARRAY) {
            pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_ARRAY);
          }
          else {
            pacx->iter = jes_get_parent_node_bytype(pacx, pacx->iter, JES_OBJECT);
          }
        }

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
  printf("\nJES::Parser error! Unexpected Token. expected: %s, got: %s \"%.*s\"",
      jes_token_type_str[token_type], jes_token_type_str[pacx->token.type], pacx->token.size,
      &pacx->json_data[pacx->token.offset]);
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
  ctx->error = 0;
  ctx->node_count = 0;

  pacx->json_data = NULL;
  pacx->json_size = 0;
  pacx->offset = -1;

  pacx->pool = (struct jes_tree_node*)(pacx + 1);
  pacx->pool_size = pool_size - sizeof(struct jes_context) - sizeof(struct jes_parser_context);
  pacx->capacity = pacx->pool_size / sizeof(struct jes_tree_node);
  printf("\nallocator capacity is %d nodes", pacx->capacity);
  pacx->allocated = 0;
  pacx->index = 0;
  pacx->iter = NULL;
  pacx->root = NULL;
  pacx->state = JES_STATE_START;
  printf("\n offset=%d", pacx->offset);
  return ctx;
}

int jes_parse(struct jes_context *ctx, char *json_data, uint32_t size)
{
  int result = 0;
  struct jes_parser_context *pacx = ctx->pacx;
  pacx->json_data = json_data;
  pacx->json_size = size;
  /* Fetch the first token to before entering the state machine. */
  pacx->token = jes_get_token(pacx);

  while (pacx->token.type != JES_TOKEN_EOF) {

    switch (pacx->state) {
      /* Only an opening bracket is acceptable in this state. */
      case JES_STATE_START:
        if (jes_expect(pacx, JES_TOKEN_OPENING_BRACKET, JES_OBJECT, JES_STATE_WANT_KEY)) {
          continue;
        }
        ctx->error = -1;
        break;

      /* An opening parenthesis has already been found.
         A closing bracket is allowed. Otherwise, only a KEY is acceptable. */
      case JES_STATE_WANT_KEY:
        if (jes_accept(pacx, JES_TOKEN_CLOSING_BRACKET, JES_NONE, JES_STATE_COMPOSITE_END)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOKEN_STRING, JES_KEY, JES_STATE_WANT_VALUE) &&
            jes_expect(pacx, JES_TOKEN_COLON, JES_NONE, JES_STATE_WANT_VALUE)) {
          continue;
        }

        ctx->error = -1;
        break;

      /* A composites can be an Object, an Array or a Key/value pair.
         When a composite is closed, another closing symbol is allowed.
         Otherwise, only a separator is acceptable. */
      case JES_STATE_COMPOSITE_END:
        if (jes_accept(pacx, JES_TOKEN_CLOSING_BRACKET, JES_NONE, JES_STATE_COMPOSITE_END) ||
            jes_accept(pacx, JES_TOKEN_CLOSING_BRACE, JES_NONE, JES_STATE_COMPOSITE_END)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOKEN_COMMA, JES_NONE, JES_STATE_WANT_KEY)) {
          continue;
        }

        break;

      case JES_STATE_WANT_VALUE:
        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACKET, JES_OBJECT, JES_STATE_WANT_KEY)) {
          continue;
        }

        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACE, JES_ARRAY, JES_STATE_WANT_ARRAY)) {
          continue;
        }

        if (jes_accept(pacx, JES_TOKEN_STRING, JES_VALUE_STRING, JES_STATE_COMPOSITE_END) ||
            jes_accept(pacx, JES_TOKEN_NUMBER, JES_VALUE_NUMBER, JES_STATE_COMPOSITE_END)   ||
            jes_accept(pacx, JES_TOKEN_BOOLEAN, JES_VALUE_BOOLEAN, JES_STATE_COMPOSITE_END) ||
            jes_accept(pacx, JES_TOKEN_NULL, JES_VALUE_NULL, JES_STATE_COMPOSITE_END)) {
          continue;
        }

        ctx->error = -1;
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

          if (jes_expect(pacx, JES_TOKEN_CLOSING_BRACE, JES_NONE, JES_STATE_COMPOSITE_END)) {
            continue;
          }
        }

      default:
        break;
    }

    break;
  }

  ctx->node_count = pacx->allocated;
  return result;
}

int jes_serialize(struct jes_context *ctx, uint8_t *buffer, uint32_t len)
{
  struct jes_tree_node *iter = ctx->pacx->root;
  uint8_t *dst = buffer;
  int result = 0;
  while (iter) {
    switch (iter->data.type) {
      case JES_OBJECT:
        dst = (uint8_t*)memcpy(dst, "{ \n", sizeof("{ \n") - 1) + (sizeof("{ \n") - 1);
        break;
      case JES_KEY:
        *dst++ = '"';
        dst = (uint8_t*)memcpy(dst, iter->data.start, iter->data.size) + iter->data.size;
        *dst++ = '"';
        *dst++ = ':';
        *dst++ = ' ';
        break;
      case JES_VALUE_STRING:
        *dst++ = '"';
        dst = (uint8_t*)memcpy(dst, iter->data.start, iter->data.size) + iter->data.size;
        *dst++ = '"';
        break;
      case JES_VALUE_NUMBER:
      case JES_VALUE_BOOLEAN:
      case JES_VALUE_NULL:
        dst = (uint8_t*)memcpy(dst, iter->data.start, iter->data.size) + iter->data.size;
        break;
      case JES_ARRAY:
        dst = (uint8_t*)memcpy(dst, "[ \n", sizeof("[ \n") - 1) + (sizeof("[ \n") - 1);
        break;
      default:
      case JES_NONE:
        printf("\n Serialize error! Node of unexpected type: %d", iter->data.type);
        return -1;
    }

    if (HAS_CHILD(iter)) {
      iter = jes_get_child_node(ctx->pacx, iter);
      continue;
    }

    if (HAS_RIGHT(iter)) {
      iter = jes_get_right_node(ctx->pacx, iter);
      *dst++ = ',';
      *dst++ = '\n';
      continue;
    }

    while (iter = jes_get_parent_node(ctx->pacx, iter)) {
      if (HAS_RIGHT(iter)) {
        iter = jes_get_right_node(ctx->pacx, iter);
        *dst++ = ',';
        *dst++ = '\n';
        break;
      }
    }

      *dst++ = '}';
  }
  *dst = '\0';
  return result;
}

struct jes_node jes_get_root(struct jes_context *ctx)
{
  if (ctx) {
    ctx->pacx->iter = ctx->pacx->root;
    return ctx->pacx->iter->data;
  }
  return (struct jes_node){ 0 };
}

struct jes_node jes_get_parent(struct jes_context *ctx)
{
  if ((ctx) && HAS_PARENT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->parent];
    return ctx->pacx->iter->data;
  }
  return (struct jes_node){ 0 };
}

struct jes_node jes_get_child(struct jes_context *ctx)
{
  if ((ctx) && HAS_CHILD(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->child];
    return ctx->pacx->iter->data;
  }
  return (struct jes_node){ 0 };
}

struct jes_node jes_get_next(struct jes_context *ctx)
{
  if ((ctx) && HAS_RIGHT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->right];
    return ctx->pacx->iter->data;
  }
  return (struct jes_node){ 0 };
}

void jes_reset_iterator(struct jes_context *ctx)
{
  ctx->pacx->iter = ctx->pacx->root;
}

void jes_print(struct jes_context *ctx)
{
  struct jes_tree_node *node = ctx->pacx->root;
  if (!ctx->pacx->root) return;
  uint32_t idx;
  for (idx = 0; idx < ctx->pacx->allocated; idx++) {
    printf("\n    %d. %s,   parent:%d, next:%d, child:%d", idx, jes_node_type_str[ctx->pacx->pool[idx].data.type],
      ctx->pacx->pool[idx].parent, ctx->pacx->pool[idx].right, ctx->pacx->pool[idx].child);
  }

  while (node) {

    if (node->data.type == JES_NONE) {
      printf("\nEND! reached a JES_NONE");
      break;
    }

    if (node->data.type == JES_OBJECT) {
      printf("\n    { <%s> - @%d", jes_node_type_str[node->data.type]);
    } else if (node->data.type == JES_KEY) {
      printf("\n        %.*s <%s>: - @%d", node->data.size, node->data.start, jes_node_type_str[node->data.type]);
    } else if (node->data.type == JES_ARRAY) {
      //printf("\n            %.*s <%s>", node.size, &ctx->json_data[node.offset], jes_node_type_str[node.type]);
    } else {
      printf("\n            %.*s <%s> - @%d", node->data.size, node->data.start, jes_node_type_str[node->data.type]);
    }

    if (HAS_CHILD(node)) {
      node = jes_get_child_node(ctx->pacx, node);
      continue;
    }

    if (HAS_RIGHT(node)) {
      node = jes_get_right_node(ctx->pacx, node);
      continue;
    }

    while (node = jes_get_parent_node(ctx->pacx, node)) {
      if (HAS_RIGHT(node)) {
        node = jes_get_right_node(ctx->pacx, node);
        break;
      }
    }
  }
}

int main(void)
{
  struct jes_context *ctx;
  FILE *fp;
  char file_data[0x4FFFF];
  uint8_t mem_pool[0x4FFFF];
  uint8_t output[0x4FFFF];

  printf("\nSize of jes_context: %d bytes", sizeof(struct jes_context));
  printf("\nSize of jes_parser_context: %d bytes", sizeof(struct jes_parser_context));
  printf("\nSize of jes_tree_node: %d bytes", sizeof(struct jes_tree_node));


  fp = fopen("test1.json", "rb");

  if (fp != NULL) {
    size_t newLen = fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }
  //printf("\n\n\n %s", file_data);
  ctx = jes_init_context(mem_pool, sizeof(mem_pool));
  printf("0x%lX, 0x%lX", mem_pool, ctx);
  if (!ctx) {
    printf("\n Context init failed!");
  }

  if (0 == jes_parse(ctx, file_data, sizeof(file_data))) {
    printf("\nSize of JSON data: %d bytes", strnlen(file_data, sizeof(file_data)));
    printf("\nMemory required: %d bytes for %d elements.", ctx->node_count*sizeof(struct jes_tree_node), ctx->node_count);

    jes_print(ctx);
    jes_serialize(ctx, output, sizeof(output));
    printf("\n\n%s", output);
    //printf("\n\n\n %s", file_data);
  }
  else {
    printf("\nFAILED");
  }
  return 0;
}