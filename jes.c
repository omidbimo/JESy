
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <assert.h>

#define LOG

#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.size = size_;

#define LOOK_AHEAD(ctx_) ctx_->json_data[ctx_->offset + 1]
#define IS_EOF_AHEAD(ctx_) (((ctx_->offset + 1) >= ctx_->size) || \
                            (ctx_->json_data[ctx_->offset + 1] == '\0'))
#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))

#define HAS_CHILD(node_ptr) (node_ptr->child > -1)
#define HAS_NEXT(node_ptr) (node_ptr->next > -1)
#define HAS_PARENT(node_ptr) (node_ptr->parent > -1)

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

enum jes_node_type {
  JES_NODE_NONE = 0,
  JES_NODE_OBJECT,
  JES_NODE_KEY,
  JES_NODE_VALUE,
  JES_NODE_ARRAY,
};

struct jes_node_data {
  uint16_t type; /* of type enum jes_node_type */
  uint16_t size;
  uint32_t offset;
};

typedef int16_t jes_node_descriptor;

struct jes_node {
  jes_node_descriptor self;
  jes_node_descriptor parent;
  jes_node_descriptor child;
  jes_node_descriptor next;
  struct jes_node_data data;
};

struct jes_token {
  enum jes_token_type type;
  uint32_t offset;
  uint32_t size;
};

struct jes_allocator {
  uint32_t size;
  uint16_t capacity;
  uint16_t allocated;
  int16_t  index;
  struct jes_node *pool;
  void *free;

};

struct jes_parser_context {
  uint8_t   *json_data;
  uint32_t  size;
  int32_t   offset;
  struct jes_token token;
  struct jes_allocator allocator;
  uint32_t mem_calc;
  uint32_t node_count;
  uint32_t last_error;
  struct jes_node *root;
  struct jes_node *node;
};

static void jes_log(struct jes_parser_context *ctx, const struct jes_token *token)
{
  printf("\n    JES::Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token->offset, token->size, jes_token_type_str[token->type],
          token->size, &ctx->json_data[token->offset]);
}

static struct jes_node* jes_allocate_node(struct jes_allocator *alloc)
{
  struct jes_node *new_node = NULL;

  if (alloc->index < alloc->capacity) {
    new_node = &alloc->pool[alloc->index];
    memset(new_node, 0, sizeof(*new_node));
    new_node->self = alloc->index;
    new_node->parent = -1;
    new_node->child = -1;
    new_node->next = -1;
    alloc->allocated++;
    alloc->index++;
  }
  return new_node;
}

static void jes_init_allocator(struct jes_allocator *alloc,
                               void *mem_pool, uint32_t pool_size)
{
  alloc->pool = mem_pool;
  alloc->size = pool_size;
  alloc->capacity = pool_size / sizeof(struct jes_node);
  printf("\nallocator capacity is %d nodes", alloc->capacity);
  alloc->allocated = 0;
  //alloc->free.jes_token_type_str = 0;
  //alloc->free.prev = 0;
  alloc->index = 0;
}

static struct jes_node* jes_get_node_parent(struct jes_allocator *alloc, struct jes_node *node)
{
  /* TODO: add checkings */
  if (node && (HAS_PARENT(node))) {
    return &alloc->pool[node->parent];
  }

  return NULL;
}

static struct jes_node* jes_get_node_child(struct jes_allocator *alloc, struct jes_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_CHILD(node)) {
    return &alloc->pool[node->child];
  }

  return NULL;
}

static struct jes_node* jes_get_node_next(struct jes_allocator *alloc, struct jes_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_NEXT(node)) {
    return &alloc->pool[node->next];
  }

  return NULL;
}

static struct jes_node* jes_get_node_object_parent(struct jes_allocator *alloc, struct jes_node *node)
{
  struct jes_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &alloc->pool[node->parent];
    if (node->data.type == JES_NODE_OBJECT) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jes_node* jes_get_node_key_parent(struct jes_allocator *alloc, struct jes_node *node)
{
  struct jes_node *parent = NULL;
  /* TODO: add checkings */
  while (HAS_PARENT(node)) {
    node = &alloc->pool[node->parent];
    if (node->data.type == JES_NODE_KEY) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jes_node* jes_get_node_array_parent(struct jes_allocator *alloc, struct jes_node *node)
{
  struct jes_node *parent = NULL;
  /* TODO: add checkings */
  while (HAS_PARENT(node)) {
    node = &alloc->pool[node->parent];
    if (node->data.type == JES_NODE_ARRAY) {
      parent = node;
      break;
    }
  }

  return parent;
}

static enum jes_node_type jes_get_parent_type(struct jes_allocator *alloc, struct jes_node *node)
{
  if (node) {
    return alloc->pool[node->parent].data.type;
  }
  return JES_NODE_NONE;
}

static struct jes_node* jes_append_node(struct jes_parser_context *ctx,
     struct jes_node *node, uint16_t type, uint32_t offset, uint16_t size)
{
  struct jes_node *new_node = jes_allocate_node(&ctx->allocator);

  if (new_node >= 0) {
    new_node->data.type = type;
    new_node->data.size = size;
    new_node->data.offset = offset;
    if (node) {
      new_node->parent = node->self;

      if (HAS_CHILD(node)) {
        struct jes_node *child = &ctx->allocator.pool[node->child];
        while (HAS_NEXT(child)) {
          child = &ctx->allocator.pool[child->next];
        }
        child->next = new_node->self;
      }
      else {
        node->child = new_node->self;
      }
    }
    else {
      assert(!ctx->root);
      ctx->root = new_node;
    }
  }

  return new_node;
}

void jes_init_context(struct jes_parser_context *ctx,
                      void *mem_pool, uint32_t pool_size)
{
  printf("\n pool size %d", pool_size);
  ctx->json_data = NULL;
  ctx->size = 0;
  ctx->offset = -1;
  jes_init_allocator(&ctx->allocator, mem_pool, pool_size);
  ctx->mem_calc = 0;
  ctx->node_count = 0;
  ctx->last_error = 0;
  ctx->root = NULL;
  ctx->node = NULL;
}

static struct jes_token jes_get_token(struct jes_parser_context *ctx)
{
  struct jes_token token = { 0 };

  while (true) {

    if (++ctx->offset >= ctx->size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = JES_TOKEN_INVALID;
      }
      break;
    }

    char ch = ctx->json_data[ctx->offset];
    //printf("\n-->\"%c\"(%d)<--@%d", ch, ch, ctx->offset);
    if (!token.type) {

      /* Reaching the end of the string. Deliver the last type detected. */
      if (ch == '\0') {
        token.offset = ctx->offset;
        break;
      }

      if (ch == '{') {
        UPDATE_TOKEN(token, JES_TOKEN_OPENING_BRACKET, ctx->offset, 1);
        break;
      }

      if (ch == '}') {
        UPDATE_TOKEN(token, JES_TOKEN_CLOSING_BRACKET, ctx->offset, 1);
        break;
      }

      if (ch == '[') {
        UPDATE_TOKEN(token, JES_TOKEN_OPENING_BRACE, ctx->offset, 1);
        break;
      }

      if (ch == ']') {
        UPDATE_TOKEN(token, JES_TOKEN_CLOSING_BRACE, ctx->offset, 1);
        break;
      }

      if (ch == ':') {
        UPDATE_TOKEN(token, JES_TOKEN_COLON, ctx->offset, 1)
        break;
      }

      if (ch == ',') {
        UPDATE_TOKEN(token, JES_TOKEN_COMMA, ctx->offset, 1)
        break;
      }

      if (ch == '\"') {
        /* Use the jes_token_type_str offset since '\"' won't be a part of token. */
        UPDATE_TOKEN(token, JES_TOEKN_STRING, ctx->offset + 1, 0);
        if (IS_EOF_AHEAD(ctx)) {
          UPDATE_TOKEN(token, JES_TOKEN_INVALID, ctx->offset, 1);
          break;
        }
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
        /* NUMBERs do not have dedicated enclosing symbols like STRINGs.
           To prevent the tokenizer to consume too much characters, we need to
           look ahead and stop the process if the jes_token_type_str character is one of
           EOF, ',', '}' or ']' */
        if (IS_EOF_AHEAD(ctx) ||
            (LOOK_AHEAD(ctx) == '}') ||
            (LOOK_AHEAD(ctx) == ']') ||
            (LOOK_AHEAD(ctx) == ',')) {
          break;
        }
        continue;
      }

      if (ch == '-') {
        if (!IS_EOF_AHEAD(ctx) && IS_DIGIT(LOOK_AHEAD(ctx))) {
          UPDATE_TOKEN(token, JES_TOKEN_NUMBER, ctx->offset, 1);
          continue;
        }
        UPDATE_TOKEN(token, JES_TOKEN_INVALID, ctx->offset, 1);
        break;
      }

      if ((ch == 't') || (ch == 'f')) {
        if ((LOOK_AHEAD(ctx) < 'a') || (LOOK_AHEAD(ctx) > 'z')) {
          UPDATE_TOKEN(token, JES_TOKEN_INVALID, ctx->offset, 1);
          break;
        }
        UPDATE_TOKEN(token, JES_TOKEN_BOOLEAN, ctx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JES_TOKEN_NULL, ctx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        continue;
      }

      UPDATE_TOKEN(token, JES_TOKEN_INVALID, ctx->offset, 1);
      break;
    }
    else if (token.type == JES_TOEKN_STRING) {

      /* We'll not deliver '\"' symbol as a part of token. */
      if (ch == '\"') {
        break;
      }

      if (ch == '\\') {
        if (LOOK_AHEAD(ctx) != 'n') {
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
        if (!IS_DIGIT(LOOK_AHEAD(ctx)) && LOOK_AHEAD(ctx) != '.') {
          break;
        }
        continue;
      }

      if (ch == '.') {
        token.size++;
        if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
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
      if ((LOOK_AHEAD(ctx) == ',') ||
          (LOOK_AHEAD(ctx) == ']') ||
          (LOOK_AHEAD(ctx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(ctx)))) {
        /* Check against "true". Use the longer string as reference. */
        uint32_t compare_size = token.size > sizeof("true") - 1
                              ? token.size
                              : sizeof("true") - 1;
        if (memcmp("true", &ctx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        /* Check against "false". Use the longer string as reference. */
        compare_size = token.size > sizeof("false") - 1
                     ? token.size
                     : sizeof("false") - 1;
        if (memcmp("false", &ctx->json_data[token.offset], compare_size) == 0) {
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
      if ((LOOK_AHEAD(ctx) == ',') ||
          (LOOK_AHEAD(ctx) == ']') ||
          (LOOK_AHEAD(ctx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(ctx)))) {
        /* Check against "null". Use the longer string as reference. */
        uint32_t compare_size = token.size > sizeof("null") - 1
                              ? token.size
                              : sizeof("null") - 1;
        if (memcmp("null", &ctx->json_data[token.offset], compare_size) == 0) {
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
    jes_log(ctx, &token);
#endif
  return token;
}

static bool jes_accept(struct jes_parser_context *ctx, enum jes_token_type token_type, enum jes_node_type node_type)
{
  if (ctx->token.type == token_type) {
    switch (node_type) {
      case JES_NODE_OBJECT:    /* Fall through intended. */
      case JES_NODE_KEY:       /* Fall through intended. */
      case JES_NODE_VALUE:     /* Fall through intended. */
      case JES_NODE_ARRAY:
        ctx->mem_calc += sizeof(struct jes_node);
        ctx->node_count++;
        ctx->node = jes_append_node(ctx, ctx->node, node_type, ctx->token.offset, ctx->token.size);
        if (!ctx->node) {
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
          ctx->node = jes_get_node_object_parent(&ctx->allocator, ctx->node);
        }
        else if (token_type == JES_TOKEN_CLOSING_BRACKET) {
          if (ctx->node->data.type != JES_NODE_OBJECT) {
            ctx->node = jes_get_node_object_parent(&ctx->allocator, ctx->node);
          }
          ctx->node = jes_get_node_object_parent(&ctx->allocator, ctx->node);
        }
        else if ((token_type == JES_TOKEN_COMMA) &&
                 (ctx->node->data.type == JES_NODE_VALUE)) {
          if (jes_get_parent_type(&ctx->allocator, ctx->node) == JES_NODE_ARRAY) {
            ctx->node = jes_get_node_array_parent(&ctx->allocator, ctx->node);
          }
          else {
            ctx->node = jes_get_node_object_parent(&ctx->allocator, ctx->node);
          }
        }

        break;
      default:

        break;
    }

    ctx->token = jes_get_token(ctx);
    return true;
  }
  return false;
}

static bool jes_expect(struct jes_parser_context *ctx, enum jes_token_type token_type, enum jes_node_type node_type)
{
  if (jes_accept(ctx, token_type, node_type)) {
    return true;
  }
  printf("\nJES::Parser error! Unexpected Token. expected: %s, got: %s \"%.*s\"",
      jes_token_type_str[token_type], jes_token_type_str[ctx->token.type], ctx->token.size,
      &ctx->json_data[ctx->token.offset]);
  return false;
}

int jes_parse(struct jes_parser_context *ctx, char *json_data, uint32_t size)
{
  ctx->json_data = json_data;
  ctx->size = size;
  int result = 0;

  ctx->token = jes_get_token(ctx);
  if (!jes_expect(ctx, JES_TOKEN_OPENING_BRACKET, JES_NODE_OBJECT)) {
    return -1;
  }

  while (ctx->node) {

    switch (ctx->node->data.type) {

      case JES_NODE_OBJECT:
        if (jes_accept(ctx, JES_TOKEN_CLOSING_BRACKET, JES_NODE_NONE) ||
            jes_accept(ctx, JES_TOKEN_COMMA, JES_NODE_NONE)) {
          continue;
        }

        if (jes_expect(ctx, JES_TOEKN_STRING, JES_NODE_KEY)) {
          continue;
        }

        result = -1;
        break;

      case JES_NODE_KEY:
        if (!jes_expect(ctx, JES_TOKEN_COLON, JES_NODE_NONE)) {
            break;
        }
        if (jes_accept(ctx, JES_TOEKN_STRING, JES_NODE_VALUE)  ||
            jes_accept(ctx, JES_TOKEN_NUMBER, JES_NODE_VALUE)  ||
            jes_accept(ctx, JES_TOKEN_BOOLEAN, JES_NODE_VALUE) ||
            jes_accept(ctx, JES_TOKEN_NULL, JES_NODE_VALUE)) {
          continue;
        }
        if (jes_accept(ctx, JES_TOKEN_OPENING_BRACE, JES_NODE_ARRAY)) {
          continue;
        }

        if (jes_expect(ctx, JES_TOKEN_OPENING_BRACKET, JES_NODE_OBJECT)) {
          continue;
        }
        result = -1;
        break;

      case JES_NODE_VALUE:
          if (jes_accept(ctx, JES_TOKEN_CLOSING_BRACE, JES_NODE_NONE) ||
              jes_accept(ctx, JES_TOKEN_CLOSING_BRACKET, JES_NODE_NONE)) {
            jes_accept(ctx, JES_TOKEN_COMMA, JES_NODE_NONE);
            continue;
          }

          if (jes_expect(ctx, JES_TOKEN_COMMA, JES_NODE_NONE)) {
            continue;
          }
        result = -1;
        break;
      case JES_NODE_ARRAY:
        if (jes_accept(ctx, JES_TOKEN_OPENING_BRACE, JES_NODE_ARRAY)) {
          continue;
        }

        if (jes_accept(ctx, JES_TOKEN_OPENING_BRACKET, JES_NODE_OBJECT)) {
          continue;
        }

        if (jes_accept(ctx, JES_TOEKN_STRING, JES_NODE_VALUE)  ||
            jes_accept(ctx, JES_TOKEN_NUMBER, JES_NODE_VALUE)  ||
            jes_accept(ctx, JES_TOKEN_BOOLEAN, JES_NODE_VALUE) ||
            jes_accept(ctx, JES_TOKEN_NULL, JES_NODE_VALUE)) {
          continue;
        }

        if (jes_expect(ctx, JES_TOKEN_CLOSING_BRACE, JES_NODE_NONE)) {
          jes_accept(ctx, JES_TOKEN_COMMA, JES_NODE_NONE);
          continue;
        }
        result = -1;
        break;

      default:
        break;
    }

    break;
  }

  return result;
}

void print_nodes(struct jes_parser_context *ctx)
{
  struct jes_node *node = ctx->root;

  uint32_t idx;
  for (idx = 0; idx < ctx->allocator.allocated; idx++) {
    printf("\n    %d. %s,   parent:%d, next:%d, child:%d", idx, jes_node_type_str[ctx->allocator.pool[idx].data.type],
      ctx->allocator.pool[idx].parent, ctx->allocator.pool[idx].next, ctx->allocator.pool[idx].child);
  }

  while (node) {

    if (node->data.type == JES_NODE_NONE) {
      printf("\nEND! reached a JES_NODE_NONE");
      break;
    }

    if (node->self == -1) {
      printf("\nEND! Reached a -1 self!");
      break;
    }

    if (node->data.type == JES_NODE_OBJECT) {
      printf("\n    { <%s>", jes_node_type_str[node->data.type]);
    } else if (node->data.type == JES_NODE_KEY) {
      printf("\n        %.*s <%s> :", node->data.size, &ctx->json_data[node->data.offset], jes_node_type_str[node->data.type]);
    } else if (node->data.type == JES_NODE_ARRAY) {
      //printf("\n            %.*s <%s>", node.size, &ctx->json_data[node.offset], jes_node_type_str[node.type]);
    } else {
      printf("\n            %.*s <%s>", node->data.size, &ctx->json_data[node->data.offset], jes_node_type_str[node->data.type]);
    }

    if (HAS_CHILD(node)) {
      node = jes_get_node_child(&ctx->allocator, node);
      continue;
    }

    if (HAS_NEXT(node)) {
      node = jes_get_node_next(&ctx->allocator, node);
      continue;
    }

    while (node = jes_get_node_parent(&ctx->allocator, node)) {
      if (HAS_NEXT(node)) {
        node = jes_get_node_next(&ctx->allocator, node);
        break;
      }
    }
  }
}

int main(void)
{
  struct jes_parser_context ctx;
  FILE *fp;
  char file_data[0x4FFFF];
  uint8_t mem_pool[0x4FFFF];

  printf("\nSize of jes_node: %d bytes", sizeof(struct jes_node));
  printf("\nSize of jes_token: %d bytes", sizeof(struct jes_token));
  printf("\nSize of jes_parser_context: %d bytes", sizeof(struct jes_parser_context));



  jes_init_context(&ctx, mem_pool, sizeof(mem_pool));
  fp = fopen("test1.json", "rb");

  if (fp != NULL) {
    size_t newLen = fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }

  if (0 == jes_parse(&ctx, file_data, sizeof(file_data))) {
    printf("\nSize of JSON data: %d bytes", strnlen(file_data, sizeof(file_data)));
    printf("\nMemory required: %d bytes for %d elements.", ctx.mem_calc, ctx.node_count);
    printf("\nMemory consumed: %d elements.", ctx.allocator.allocated);
    print_nodes(&ctx);
  }
  else {
    printf("\nFAILED");
  }
  return 0;
}