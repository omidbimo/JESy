
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
#define HAS_NEXT(node_ptr) (node_ptr->next > -1)
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
  "VALUE",
  "ARRAY",
};

enum jes_token_type {
  JES_TOKEN_EOF = 0,
  JES_TOKEN_OPENING_BRACKET,
  JES_TOKEN_CLOSING_BRACKET,
  JES_TOKEN_OPENING_BRACE,
  JES_TOKEN_CLOSING_BRACE,
  JES_TOEKN_STRING,
  JES_TOKEN_NUMBER,
  JES_TOKEN_BOOLEAN,
  JES_TOKEN_NULL,
  JES_TOKEN_COLON,
  JES_TOKEN_COMMA,
  JES_TOKEN_ESC,
  JES_TOKEN_INVALID,
};

typedef int16_t jes_node_descriptor;

struct jes_tree_node {
  jes_node_descriptor self;
  jes_node_descriptor parent;
  jes_node_descriptor child;
  jes_node_descriptor next;
  struct jes_node data;
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
  struct jes_token token;
  struct jes_tree_node *iter;
  struct jes_tree_node *root;
  struct jes_tree_node *pool;
  void  *free;
};

static void jes_log(struct jes_parser_context *ctx, const struct jes_token *token)
{
  printf("\n    JES::Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token->offset, token->size, jes_token_type_str[token->type],
          token->size, &ctx->json_data[token->offset]);
}

static struct jes_tree_node* jes_allocate_node(struct jes_parser_context *pacx)
{
  struct jes_tree_node *new_node = NULL;

  if (pacx->index < pacx->capacity) {
    new_node = &pacx->pool[pacx->index];
    memset(new_node, 0, sizeof(*new_node));
    new_node->self = pacx->index;
    new_node->parent = -1;
    new_node->child = -1;
    new_node->next = -1;
    pacx->allocated++;
    pacx->index++;
  }
  return new_node;
}

static struct jes_tree_node* jes_get_node_parent(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  /* TODO: add checkings */
  if (node && (HAS_PARENT(node))) {
    return &pacx->pool[node->parent];
  }

  return NULL;
}

static struct jes_tree_node* jes_get_node_child(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_CHILD(node)) {
    return &pacx->pool[node->child];
  }

  return NULL;
}

static struct jes_tree_node* jes_get_node_next(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_NEXT(node)) {
    return &pacx->pool[node->next];
  }

  return NULL;
}

static struct jes_tree_node* jes_get_node_object_parent(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  struct jes_tree_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &pacx->pool[node->parent];
    if (node->data.type == JES_NODE_OBJECT) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jes_tree_node* jes_get_node_key_parent(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  struct jes_tree_node *parent = NULL;
  /* TODO: add checkings */
  while (HAS_PARENT(node)) {
    node = &pacx->pool[node->parent];
    if (node->data.type == JES_NODE_KEY) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jes_tree_node* jes_get_node_array_parent(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  struct jes_tree_node *parent = NULL;
  /* TODO: add checkings */
  while (HAS_PARENT(node)) {
    node = &pacx->pool[node->parent];
    if (node->data.type == JES_NODE_ARRAY) {
      parent = node;
      break;
    }
  }

  return parent;
}

static enum jes_node_type jes_get_parent_type(struct jes_parser_context *pacx, struct jes_tree_node *node)
{
  if (node) {
    return pacx->pool[node->parent].data.type;
  }
  return JES_NODE_NONE;
}

static struct jes_tree_node* jes_append_node(struct jes_parser_context *pacx,
     struct jes_tree_node *node, uint16_t type, uint32_t offset, uint16_t size)
{
  struct jes_tree_node *new_node = jes_allocate_node(pacx);

  if (new_node >= 0) {
    new_node->data.type = type;
    new_node->data.size = size;
    new_node->data.start = &pacx->json_data[offset];
    if (node) {
      new_node->parent = node->self;

      if (HAS_CHILD(node)) {
        struct jes_tree_node *child = &pacx->pool[node->child];
        while (HAS_NEXT(child)) {
          child = &pacx->pool[child->next];
        }
        child->next = new_node->self;
      }
      else {
        node->child = new_node->self;
      }
    }
    else {
      assert(!pacx->root);
      pacx->root = new_node;
    }
  }

  return new_node;
}

static struct jes_token jes_get_token(struct jes_parser_context *pacx)
{
  struct jes_token token = { 0 };

  while (true) {

    if (++pacx->offset >= pacx->pool_size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = JES_TOKEN_INVALID;
      }
      break;
    }

    char ch = pacx->json_data[pacx->offset];
    //printf("\n-->\"%c\"(%d)<--@%d", ch, ch, pacx->offset);
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
        UPDATE_TOKEN(token, JES_TOEKN_STRING, pacx->offset + 1, 0);
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
    else if (token.type == JES_TOEKN_STRING) {

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

static bool jes_accept(struct jes_parser_context *pacx,
                  enum jes_token_type token_type, enum jes_node_type node_type)
{
  if (pacx->token.type == token_type) {
    switch (node_type) {
      case JES_NODE_OBJECT:         /* Fall through intended. */
      case JES_NODE_KEY:            /* Fall through intended. */
      case JES_NODE_VALUE_STRING:   /* Fall through intended. */
      case JES_NODE_VALUE_NUMBER:   /* Fall through intended. */
      case JES_NODE_VALUE_BOOLEAN:  /* Fall through intended. */
      case JES_NODE_VALUE_NULL:     /* Fall through intended. */
      case JES_NODE_ARRAY:
        pacx->iter = jes_append_node(pacx, pacx->iter, node_type, pacx->token.offset, pacx->token.size);
        if (!pacx->iter) {
          printf("\n JES Allocation failed");
          return false;
        }

#ifdef LOG
        printf("\n    JES::Node: %s", jes_node_type_str[node_type]);
#endif
        break;

      case JES_NODE_NONE:
        /* Some tokens signaling an upward move through the tree.
           A ']' indicates the end of an Array and thus the end of a key:value
                 pair. Go back to the parent object.
           A '}' indicates the end of an object. Go back to the parent object
           A ',' indicates the end of a value.
                 if the value is a part of an array, go back parent array node.
                 otherwise, go back to the parent object.
        */
        if (token_type == JES_TOKEN_CLOSING_BRACE) {
          pacx->iter = jes_get_node_object_parent(pacx, pacx->iter);
        }
        else if (token_type == JES_TOKEN_CLOSING_BRACKET) {
          if (pacx->iter->data.type != JES_NODE_OBJECT) {
            pacx->iter = jes_get_node_object_parent(pacx, pacx->iter);
          }
          pacx->iter = jes_get_node_object_parent(pacx, pacx->iter);
        }
        else if ((token_type == JES_TOKEN_COMMA) &&
                 (pacx->iter->data.type == JES_NODE_VALUE_STRING  ||
                  pacx->iter->data.type == JES_NODE_VALUE_NUMBER  ||
                  pacx->iter->data.type == JES_NODE_VALUE_BOOLEAN ||
                  pacx->iter->data.type == JES_NODE_VALUE_NULL)) {
          if (jes_get_parent_type(pacx, pacx->iter) == JES_NODE_ARRAY) {
            pacx->iter = jes_get_node_array_parent(pacx, pacx->iter);
          }
          else {
            pacx->iter = jes_get_node_object_parent(pacx, pacx->iter);
          }
        }

        break;
      default:

        break;
    }

    pacx->token = jes_get_token(pacx);
    return true;
  }
  return false;
}

static bool jes_expect(struct jes_parser_context *pacx, enum jes_token_type token_type, enum jes_node_type node_type)
{
  if (jes_accept(pacx, token_type, node_type)) {
    return true;
  }
  printf("\nJES::Parser error! Unexpected Token. expected: %s, got: %s \"%.*s\"",
      jes_token_type_str[token_type], jes_token_type_str[pacx->token.type], pacx->token.size,
      &pacx->json_data[pacx->token.offset]);
  return false;
}

static void jes_init_parser(struct jes_parser_context *pacx,
                            char *json_data, uint32_t json_size,
                            void *mem_pool, uint32_t pool_size)
{
  pacx->json_data = json_data;
  pacx->json_size = json_size;
  pacx->offset = -1;

  pacx->pool = mem_pool;
  pacx->pool_size = pool_size;
  pacx->capacity = pool_size / sizeof(struct jes_tree_node);
  printf("\nallocator capacity is %d nodes", pacx->capacity);
  pacx->allocated = 0;
  pacx->index = 0;
  pacx->iter = NULL;
  pacx->root = NULL;
}

struct jes_context* jes_parse(char *json_data, uint32_t size,
                              void *mem_pool, uint32_t pool_size)
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
  jes_init_parser(pacx, json_data, size, pacx + 1,
    pool_size - sizeof(struct jes_context) - sizeof(struct jes_parser_context));

  /* Start parsing. Internal members of context: token and node have both
     invalid values. Call get_token and accept once to setup these members. */
  pacx->token = jes_get_token(pacx);
  if (!jes_expect(pacx, JES_TOKEN_OPENING_BRACKET, JES_NODE_OBJECT)) {
    ctx->error -1;
    return ctx;
  }

  while (pacx->iter) {

    switch (pacx->iter->data.type) {

      case JES_NODE_OBJECT:
        if (jes_accept(pacx, JES_TOKEN_CLOSING_BRACKET, JES_NODE_NONE) ||
            jes_accept(pacx, JES_TOKEN_COMMA, JES_NODE_NONE)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOEKN_STRING, JES_NODE_KEY)) {
          continue;
        }

        ctx->error -1;
        break;

      case JES_NODE_KEY:
        if (!jes_expect(pacx, JES_TOKEN_COLON, JES_NODE_NONE)) {
            break;
        }
        if (jes_accept(pacx, JES_TOEKN_STRING, JES_NODE_VALUE_STRING)  ||
            jes_accept(pacx, JES_TOKEN_NUMBER, JES_NODE_VALUE_NUMBER)  ||
            jes_accept(pacx, JES_TOKEN_BOOLEAN, JES_NODE_VALUE_BOOLEAN) ||
            jes_accept(pacx, JES_TOKEN_NULL, JES_NODE_VALUE_NULL)    ||
            jes_accept(pacx, JES_TOKEN_OPENING_BRACE, JES_NODE_ARRAY)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOKEN_OPENING_BRACKET, JES_NODE_OBJECT)) {
          continue;
        }

        ctx->error -1;
        break;

      case JES_NODE_VALUE_STRING:
      case JES_NODE_VALUE_NUMBER:
      case JES_NODE_VALUE_BOOLEAN:
      case JES_NODE_VALUE_NULL:
          if (jes_accept(pacx, JES_TOKEN_CLOSING_BRACE, JES_NODE_NONE) ||
              jes_accept(pacx, JES_TOKEN_CLOSING_BRACKET, JES_NODE_NONE)) {
            jes_accept(pacx, JES_TOKEN_COMMA, JES_NODE_NONE);
            continue;
          }

          if (jes_expect(pacx, JES_TOKEN_COMMA, JES_NODE_NONE)) {
            continue;
          }
        ctx->error -1;
        break;
      case JES_NODE_ARRAY:
        if (jes_accept(pacx, JES_TOKEN_OPENING_BRACE, JES_NODE_ARRAY)    ||
            jes_accept(pacx, JES_TOKEN_OPENING_BRACKET, JES_NODE_OBJECT) ||
            jes_accept(pacx, JES_TOEKN_STRING, JES_NODE_VALUE_STRING)    ||
            jes_accept(pacx, JES_TOKEN_NUMBER, JES_NODE_VALUE_NUMBER)    ||
            jes_accept(pacx, JES_TOKEN_BOOLEAN, JES_NODE_VALUE_BOOLEAN)  ||
            jes_accept(pacx, JES_TOKEN_NULL, JES_NODE_VALUE_NULL)) {
          continue;
        }

        if (jes_expect(pacx, JES_TOKEN_CLOSING_BRACE, JES_NODE_NONE)) {
          jes_accept(pacx, JES_TOKEN_COMMA, JES_NODE_NONE);
          continue;
        }
        ctx->error -1;
        break;

      default:
        break;
    }

    break;
  }

  ctx->node_count = pacx->allocated;
  return ctx;
}

int main(void)
{
  struct jes_context *ctx;
  FILE *fp;
  char file_data[0x4FFFF];
  uint8_t mem_pool[0x4FFFF];

  printf("\nSize of jes_tree_node: %d bytes", sizeof(struct jes_tree_node));
  printf("\nSize of jes_token: %d bytes", sizeof(struct jes_token));
  printf("\nSize of jes_parser_context: %d bytes", sizeof(struct jes_parser_context));

  fp = fopen("test1.json", "rb");

  if (fp != NULL) {
    size_t newLen = fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }

  ctx = jes_parse(file_data, sizeof(file_data), mem_pool, sizeof(mem_pool));
  if (ctx) {
    printf("\nSize of JSON data: %d bytes", strnlen(file_data, sizeof(file_data)));
    printf("\nMemory required: %d bytes for %d elements.", ctx->node_count*sizeof(struct jes_tree_node), ctx->node_count);
    //jes_print(ctx);
  }
  else {
    printf("\nFAILED");
  }
  return 0;
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
  if ((ctx) && HAS_NEXT(ctx->pacx->iter)) {
    ctx->pacx->iter = &ctx->pacx->pool[ctx->pacx->iter->next];
    return ctx->pacx->iter->data;
  }
  return (struct jes_node){ 0 };
}

void jes_reset_iterator(struct jes_context *ctx)
{
  ctx->pacx->iter = ctx->pacx->root;
}