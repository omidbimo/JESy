
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jesy.h"

#ifdef JESY_32BIT_NODE_DESCRIPTOR
  #define JESY_INVALID_INDEX 0xFFFFFFFF
  #define JESY_MAX_VALUE_LEN 0xFFFFFFFF
#else
  #define JESY_INVALID_INDEX 0xFFFF
  #define JESY_MAX_VALUE_LEN 0xFFFF
#endif

#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.length = size_;

#define LOOK_AHEAD(ctx_) ctx_->json_data[ctx_->offset + 1]
#define IS_EOF_AHEAD(ctx_) (((ctx_->offset + 1) >= ctx_->json_size) || \
                            (ctx_->json_data[ctx_->offset + 1] == '\0'))
#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))

#define HAS_CHILD(node_ptr) (node_ptr->child < JESY_INVALID_INDEX)
#define HAS_LEFT(node_ptr) (node_ptr->left < JESY_INVALID_INDEX)
#define HAS_RIGHT(node_ptr) (node_ptr->right < JESY_INVALID_INDEX)
#define HAS_PARENT(node_ptr) (node_ptr->parent < JESY_INVALID_INDEX)

static struct jesy_node *jesy_find_duplicate_key(struct jesy_context *ctx,
                                                 struct jesy_node *object_node,
                                                 struct jesy_token *key_token);

static struct jesy_node* jesy_allocate(struct jesy_context *ctx)
{
  struct jesy_node *new_node = NULL;

  if (ctx->node_count < ctx->capacity) {
    if (ctx->free) {
      /* Pop the first node from free list */
      new_node = (struct jesy_node*)ctx->free;
      ctx->free = ctx->free->next;
    }
    else {
      assert(ctx->index < ctx->capacity);
      new_node = &ctx->pool[ctx->index];
      ctx->index++;
    }
    /* Setting node descriptors to their default values. */
    memset(new_node, 0xFF, sizeof(jesy_node_descriptor) * 4);
    ctx->node_count++;
  }
  else {
    ctx->status = JESY_OUT_OF_MEMORY;
  }

  return new_node;
}

static void jesy_free(struct jesy_context *ctx, struct jesy_node *node)
{
  struct jesy_free_node *free_node = (struct jesy_free_node*)node;

  assert(node >= ctx->pool);
  assert(node < (ctx->pool + ctx->capacity));
  assert(ctx->node_count > 0);

  if (ctx->node_count > 0) {
    free_node->next = NULL;
    ctx->node_count--;
    /* prepend the node to the free LIFO */
    if (ctx->free) {
      free_node->next = ctx->free->next;
    }
    ctx->free = free_node;
  }
}

static struct jesy_node* jesy_get_parent_node(struct jesy_context *ctx,
                                                 struct jesy_node *node)
{
  /* TODO: add checking */
  if (node && (HAS_PARENT(node))) {
    return &ctx->pool[node->parent];
  }

  return NULL;
}

static struct jesy_node* jesy_get_parent_node_bytype(struct jesy_context *ctx,
                                                     struct jesy_node *node,
                                                     enum jesy_node_type type)
{
  struct jesy_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &ctx->pool[node->parent];
    if (node->data.type == type) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jesy_node* jesy_get_structure_parent_node(struct jesy_context *ctx,
                                                      struct jesy_node *node)
{
  struct jesy_node *parent = NULL;
  /* TODO: add checkings */
  while (node && HAS_PARENT(node)) {
    node = &ctx->pool[node->parent];
    if ((node->data.type == JESY_OBJECT) || (node->data.type == JESY_ARRAY)) {
      parent = node;
      break;
    }
  }

  return parent;
}

static struct jesy_node* jesy_get_child_node(struct jesy_context *ctx,
                                                struct jesy_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_CHILD(node)) {
    return &ctx->pool[node->child];
  }

  return NULL;
}

static struct jesy_node* jesy_get_right_node(struct jesy_context *ctx,
                                                struct jesy_node *node)
{
  /* TODO: add checkings */
  if (node && HAS_RIGHT(node)) {
    return &ctx->pool[node->right];
  }

  return NULL;
}

static struct jesy_node* jesy_add_node(struct jesy_context *ctx,
                                       struct jesy_node *node,
                                       uint16_t type,
                                       uint32_t offset,
                                       uint16_t length)
{
  struct jesy_node *new_node = jesy_allocate(ctx);

  if (new_node) {
    new_node->data.type = type;
    new_node->data.length = length;
    new_node->data.value = &ctx->json_data[offset];

    if (node) {
      new_node->parent = (jesy_node_descriptor)(node - ctx->pool); /* node's index */

      if (HAS_CHILD(node)) {
        struct jesy_node *child = &ctx->pool[node->child];

        if (HAS_LEFT(child)) {
          struct jesy_node *last = &ctx->pool[child->left];
          last->right = (jesy_node_descriptor)(new_node - ctx->pool); /* new_node's index */
        }
        else {
          child->right = (jesy_node_descriptor)(new_node - ctx->pool); /* new_node's index */
          //new_node->left = (jesy_node_descriptor)(child - ctx->pool); /* new_node's index */
        }
        child->left = (jesy_node_descriptor)(new_node - ctx->pool); /* new_node's index */
      }
      else {
        node->child = (jesy_node_descriptor)(new_node - ctx->pool); /* new_node's index */
      }
    }
    else {
      assert(!ctx->root);
      ctx->root = new_node;
    }
  }

  return new_node;
}

static void jesy_delete_node(struct jesy_context *ctx, struct jesy_node *node)
{
  struct jesy_node *iter = jesy_get_child_node(ctx, node);
  struct jesy_node *parent = NULL;
  struct jesy_node *prev = NULL;

  do {
    iter = node;
    prev = iter;
    parent = jesy_get_parent_node(ctx, iter);

    while (true) {
      while (HAS_RIGHT(iter)) {
        prev = iter;
        iter = jesy_get_right_node(ctx, iter);
      }

      if (HAS_CHILD(iter)) {
        iter = jesy_get_child_node(ctx, iter);
        parent = jesy_get_parent_node(ctx, iter);
        continue;
      }

      break;
    }
    if (prev)prev->right = -1;
    if (parent)parent->child = -1;
    if (ctx->root == iter) {
      ctx->root = NULL;
    }

    jesy_free(ctx, iter);

  } while (iter != node);
}

static struct jesy_token jesy_get_token(struct jesy_context *ctx)
{
  struct jesy_token token = { 0 };

  while (true) {

    if (++ctx->offset >= ctx->json_size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = JESY_TOKEN_INVALID;
      }
      break;
    }

    char ch = ctx->json_data[ctx->offset];

    if (!token.type) {
      /* Reaching the end of the string. Deliver the last type detected. */
      if (ch == '\0') {
        token.offset = ctx->offset;
        break;
      }

      if (ch == '{') {
        UPDATE_TOKEN(token, JESY_TOKEN_OPENING_BRACKET, ctx->offset, 1);
        break;
      }

      if (ch == '}') {
        UPDATE_TOKEN(token, JESY_TOKEN_CLOSING_BRACKET, ctx->offset, 1);
        break;
      }

      if (ch == '[') {
        UPDATE_TOKEN(token, JESY_TOKEN_OPENING_BRACE, ctx->offset, 1);
        break;
      }

      if (ch == ']') {
        UPDATE_TOKEN(token, JESY_TOKEN_CLOSING_BRACE, ctx->offset, 1);
        break;
      }

      if (ch == ':') {
        UPDATE_TOKEN(token, JESY_TOKEN_COLON, ctx->offset, 1)
        break;
      }

      if (ch == ',') {
        UPDATE_TOKEN(token, JESY_TOKEN_COMMA, ctx->offset, 1)
        break;
      }

      if (ch == '\"') {
        /* Use the jesy_token_type_str offset since '\"' won't be a part of token. */
        UPDATE_TOKEN(token, JESY_TOKEN_STRING, ctx->offset + 1, 0);
        if (IS_EOF_AHEAD(ctx)) {
          UPDATE_TOKEN(token, JESY_TOKEN_INVALID, ctx->offset, 1);
          break;
        }
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JESY_TOKEN_NUMBER, ctx->offset, 1);
        /* NUMBERs do not have dedicated enclosing symbols like STRINGs.
           To prevent the tokenizer to consume too much characters, we need to
           look ahead and stop the process if the jesy_token_type_str character is one of
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
          UPDATE_TOKEN(token, JESY_TOKEN_NUMBER, ctx->offset, 1);
          continue;
        }
        UPDATE_TOKEN(token, JESY_TOKEN_INVALID, ctx->offset, 1);
        break;
      }

      if ((ch == 't') || (ch == 'f')) {
        if ((LOOK_AHEAD(ctx) < 'a') || (LOOK_AHEAD(ctx) > 'z')) {
          UPDATE_TOKEN(token, JESY_TOKEN_INVALID, ctx->offset, 1);
          break;
        }
        UPDATE_TOKEN(token, JESY_TOKEN_BOOLEAN, ctx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JESY_TOKEN_NULL, ctx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        continue;
      }

      UPDATE_TOKEN(token, JESY_TOKEN_INVALID, ctx->offset, 1);
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
        if (!IS_DIGIT(LOOK_AHEAD(ctx)) && LOOK_AHEAD(ctx) != '.') {
          break;
        }
        continue;
      }

      if (ch == '.') {
        token.length++;
        if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
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
      if ((LOOK_AHEAD(ctx) == ',') ||
          (LOOK_AHEAD(ctx) == ']') ||
          (LOOK_AHEAD(ctx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(ctx)))) {
        /* Check against "true". Use the longer string as reference. */
        uint32_t compare_size = token.length > sizeof("true") - 1
                              ? token.length
                              : sizeof("true") - 1;
        if (memcmp("true", &ctx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        /* Check against "false". Use the longer string as reference. */
        compare_size = token.length > sizeof("false") - 1
                     ? token.length
                     : sizeof("false") - 1;
        if (memcmp("false", &ctx->json_data[token.offset], compare_size) == 0) {
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
      if ((LOOK_AHEAD(ctx) == ',') ||
          (LOOK_AHEAD(ctx) == ']') ||
          (LOOK_AHEAD(ctx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(ctx)))) {
        /* Check against "null". Use the longer string as reference. */
        uint32_t compare_size = token.length > sizeof("null") - 1
                              ? token.length
                              : sizeof("null") - 1;
        if (memcmp("null", &ctx->json_data[token.offset], compare_size) == 0) {
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

  JESY_LOG_TOKEN(token.type, token.offset, token.length, &ctx->json_data[token.offset]);

  return token;
}

static enum jesy_node_type jesy_get_parent_type(struct jesy_context *ctx,
                                              struct jesy_node *node)
{
  if (node) {
    return ctx->pool[node->parent].data.type;
  }
  return JESY_NONE;
}

static struct jesy_node *jesy_find_duplicate_key(struct jesy_context *ctx,
              struct jesy_node *object_node, struct jesy_token *key_token)
{
  struct jesy_node *duplicate = NULL;

  if (object_node->data.type == JESY_OBJECT)
  {
    struct jesy_node *iter = object_node;
    if (HAS_CHILD(iter)) {
      iter = jesy_get_child_node(ctx, iter);
      assert(iter->data.type == JESY_KEY);
      if ((iter->data.length == key_token->length) &&
          (memcmp(iter->data.value, &ctx->json_data[key_token->offset], key_token->length) == 0)) {
        duplicate = iter;
      }
      else {
        while (HAS_RIGHT(iter)) {
          iter = jesy_get_right_node(ctx, iter);
          if ((iter->data.length == key_token->length) &&
              (memcmp(iter->data.value, &ctx->json_data[key_token->offset], key_token->length) == 0)) {
            duplicate = iter;
          }
        }
      }
    }
  }
  return duplicate;
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
  if (ctx->token.type == token_type) {
    struct jesy_node *new_node = NULL;
    //printf("\n     Parser State: %s", jesy_state_str[ctx->state]);
    if (node_type == JESY_KEY) {
#ifdef JESY_OVERWRITE_DUPLICATE_KEYS
      /* No duplicate keys in the same object are allowed.
         Only the last key:value will be reported if the keys are duplicated. */
      struct jesy_node *node = jesy_find_duplicate_key(ctx, ctx->iter, &ctx->token);
      if (node) {
        jesy_delete_node(ctx, jesy_get_child_node(ctx, node));
        ctx->iter = node;
      }
      else
#endif
      {
        new_node = jesy_add_node(ctx, ctx->iter, node_type, ctx->token.offset, ctx->token.length);
      }
    }
    else if ((node_type == JESY_OBJECT) ||
             (node_type == JESY_ARRAY)) {
      new_node = jesy_add_node(ctx, ctx->iter, node_type, ctx->token.offset, ctx->token.length);
    }
    else if (node_type == JESY_VALUE_STRING) {
      new_node = jesy_add_node(ctx, ctx->iter, node_type, ctx->token.offset, ctx->token.length);
    }
    else if ((node_type == JESY_VALUE_NUMBER)  ||
             (node_type == JESY_VALUE_BOOLEAN) ||
             (node_type == JESY_VALUE_NULL)) {
      new_node = jesy_add_node(ctx, ctx->iter, node_type, ctx->token.offset, ctx->token.length);
    }
    else { /* JESY_NONE */
       /* None-Key/Value tokens trigger upward iteration to the parent node.
       A ']' indicates the end of an Array and consequently the end of a key:value
             pair. Go back to the parent node.
       A '}' indicates the end of an object. Go back to the parent node
       A ',' indicates the end of a value.
             if the value is a part of an array, go back parent array node.
             otherwise, go back to the parent object.
      */
      if (token_type == JESY_TOKEN_CLOSING_BRACE) {
        /* [] (empty array) is a special case that needs no iteration in the
           direction the parent node. */
        if (ctx->iter->data.type != JESY_ARRAY) {
          ctx->iter = jesy_get_parent_node_bytype(ctx, ctx->iter, JESY_ARRAY);
        }
        ctx->iter = jesy_get_structure_parent_node(ctx, ctx->iter);
      }
      else if (token_type == JESY_TOKEN_CLOSING_BRACKET) {
        /* {} (empty object)is a special case that needs no iteration in the
           direction the parent node. */
        if (ctx->iter->data.type != JESY_OBJECT) {
          ctx->iter = jesy_get_parent_node_bytype(ctx, ctx->iter, JESY_OBJECT);
        }
        ctx->iter = jesy_get_structure_parent_node(ctx, ctx->iter);
      }
      else if (token_type == JESY_TOKEN_COMMA) {
        if ((ctx->iter->data.type != JESY_OBJECT) &&
            (ctx->iter->data.type != JESY_ARRAY)) {
          ctx->iter = jesy_get_structure_parent_node(ctx, ctx->iter);
        }
      }
    }

    if (ctx->status) return true;
    if (new_node) {
      ctx->iter = new_node;
      JESY_LOG_NODE(ctx->iter - ctx->pool, ctx->iter->data.type,
                    ctx->iter->parent, ctx->iter->right, ctx->iter->child);
    }

    ctx->state = state;
  //printf("   --->     Parser State: %s", jesy_state_str[ctx->state]);
    ctx->token = jesy_get_token(ctx);
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
      jesy_token_type_str[ctx->token.type], ctx->token.length,
      &ctx->json_data[ctx->token.offset]);
  printf("     Parser State: %s", jesy_state_str[ctx->state]);
#endif
  }
  return false;
}

struct jesy_context* jesy_init_context(void *mem_pool, uint32_t pool_size)
{
  if (pool_size < sizeof(struct jesy_context)) {
    return NULL;
  }

  struct jesy_context *ctx = mem_pool;

  ctx->status = 0;
  ctx->node_count = 0;

  ctx->json_data = NULL;
  ctx->json_size = 0;
  ctx->offset = (uint32_t)-1;
  ctx->index = 0;
  ctx->pool = (struct jesy_node*)(ctx + 1);
  ctx->pool_size = pool_size - (uint32_t)(sizeof(struct jesy_context));
  ctx->capacity = (ctx->pool_size / sizeof(struct jesy_node)) < JESY_INVALID_INDEX
                 ? (jesy_node_descriptor)(ctx->pool_size / sizeof(struct jesy_node))
                 : JESY_INVALID_INDEX -1;

  ctx->iter = NULL;
  ctx->root = NULL;
  ctx->state = JESY_STATE_START;

#ifndef NDEBUG
  printf("\nallocator capacity is %d nodes", ctx->capacity);
#endif

  return ctx;
}

uint32_t jesy_parse(struct jesy_context *ctx, char *json_data, uint32_t json_length)
{
  ctx->json_data = json_data;
  ctx->json_size = json_length;
  /* Fetch the first token to before entering the state machine. */
  ctx->token = jesy_get_token(ctx);

  do {
    if (ctx->token.type == JESY_TOKEN_EOF) { break; }
    //if (ctx->iter)printf("\n    State: %s, node: %s", jesy_state_str[ctx->state], jesy_node_type_str[ctx->iter->data.type]);
    switch (ctx->state) {
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

        if (!jesy_expect(ctx, JESY_TOKEN_STRING, JESY_KEY, JESY_STATE_WANT_VALUE)) {
          break;
        }
        jesy_expect(ctx, JESY_TOKEN_COLON, JESY_NONE, JESY_STATE_WANT_VALUE);
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
        if (ctx->iter->data.type == JESY_OBJECT) {
          if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACKET, JESY_NONE, JESY_STATE_STRUCTURE_END)) {
            break;
          }
          jesy_expect(ctx, JESY_TOKEN_COMMA, JESY_NONE, JESY_STATE_WANT_KEY);
        }
        else if(ctx->iter->data.type == JESY_ARRAY) {
          if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACE, JESY_NONE, JESY_STATE_STRUCTURE_END)) {
            break;
          }
          jesy_expect(ctx, JESY_TOKEN_COMMA, JESY_NONE, JESY_STATE_WANT_ARRAY);
        }
        else {
          assert(0);
        }

        break;

      default:
        assert(0);
        break;
    }
  } while ((ctx->iter) && (ctx->status == 0));

  if (ctx->status == 0) {
    if (ctx->token.type != JESY_TOKEN_EOF) {
      ctx->status = JESY_UNEXPECTED_TOKEN;
    }
    else if (ctx->iter) {
      ctx->status = JESY_UNEXPECTED_EOF;
    }
  }

  ctx->iter = ctx->root;
  return ctx->status;
}

uint32_t jesy_get_dump_size(struct jesy_context *ctx)
{
  struct jesy_node *iter = ctx->root;
  uint32_t dump_size = 0;

  while (iter) {

    if (iter->data.type == JESY_OBJECT) {
      dump_size++; /* '{' */
    }
    else if (iter->data.type == JESY_KEY) {
      dump_size += (iter->data.length + sizeof(char) * 3);/* +1 for ':' +2 for "" */
    }
    else if (iter->data.type == JESY_VALUE_STRING) {
      dump_size += (iter->data.length + sizeof(char) * 2);/* +2 for "" */
    }
    else if ((iter->data.type == JESY_VALUE_NUMBER)  ||
             (iter->data.type == JESY_VALUE_BOOLEAN) ||
             (iter->data.type == JESY_VALUE_NULL)) {
      dump_size += iter->data.length;
    }
    else if (iter->data.type == JESY_ARRAY) {
      dump_size++; /* '[' */
    }
    else {
      assert(0);
      return 0;
    }

    if (HAS_CHILD(iter)) {
      iter = jesy_get_child_node(ctx, iter);
      continue;
    }

    if (iter->data.type == JESY_OBJECT) {
      dump_size++; /* '}' */
    }

    else if (iter->data.type == JESY_ARRAY) {
      dump_size++; /* ']' */
    }

    if (HAS_RIGHT(iter)) {
      iter = jesy_get_right_node(ctx, iter);
      dump_size++; /* ',' */
      continue;
    }

    while ((iter = jesy_get_parent_node(ctx, iter))) {
      if (iter->data.type == JESY_OBJECT) {
        dump_size++; /* '}' */
      }
      else if (iter->data.type == JESY_ARRAY) {
        dump_size++; /* ']' */
      }
      if (HAS_RIGHT(iter)) {
        iter = jesy_get_right_node(ctx, iter);
        dump_size++; /* ',' */
        break;
      }
    }
  }
  return dump_size;
}

uint32_t jesy_render(struct jesy_context *ctx, char *buffer, uint32_t length)
{

  char *dst = buffer;
  char *end = buffer + length;
  struct jesy_node *iter = ctx->root;
  uint32_t required_buffer = 0;

  required_buffer = jesy_get_dump_size(ctx);
  if (required_buffer == 0) {
    ctx->status = JESY_SERIALIZE_FAILED;
    return 0;
  }
  if (length < required_buffer) {
    ctx->status = JESY_OUT_OF_MEMORY;
    return 0;
  }

  while (iter) {

    if (iter->data.type == JESY_OBJECT) {
      *dst++ = '{';
    }
    else if (iter->data.type == JESY_KEY) {
      *dst++ = '"';
      dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
      *dst++ = '"';
      *dst++ = ':';
    }
    else if (iter->data.type == JESY_VALUE_STRING) {
      *dst++ = '"';
      dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
      *dst++ = '"';
    }
    else if ((iter->data.type == JESY_VALUE_NUMBER)  ||
             (iter->data.type == JESY_VALUE_BOOLEAN) ||
             (iter->data.type == JESY_VALUE_NULL)) {
      dst = (char*)memcpy(dst, iter->data.value, iter->data.length) + iter->data.length;
    }
    else if (iter->data.type == JESY_ARRAY) {
      *dst++ = '[';
    }
    else {
      assert(0);
      ctx->status = JESY_UNEXPECTED_NODE;
      break;
    }

    if (HAS_CHILD(iter)) {
      iter = jesy_get_child_node(ctx, iter);
      continue;
    }

    if (iter->data.type == JESY_OBJECT) {
      *dst++ = '}';
    }

    else if (iter->data.type == JESY_ARRAY) {
      *dst++ = ']';
    }

    if (HAS_RIGHT(iter)) {
      iter = jesy_get_right_node(ctx, iter);
      *dst++ = ',';
      continue;
    }

     while ((iter = jesy_get_parent_node(ctx, iter))) {
      if (iter->data.type == JESY_OBJECT) {
        *dst++ = '}';
      }
      else if (iter->data.type == JESY_ARRAY) {
        *dst++ = ']';
      }
      if (HAS_RIGHT(iter)) {
        iter = jesy_get_right_node(ctx, iter);
        *dst++ = ',';
        break;
      }
    }
  }

  return dst - buffer;
}

struct jessy_element jesy_get_root(struct jesy_context *ctx)
{
  if (ctx) {
    ctx->iter = ctx->root;
    return ctx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

struct jessy_element jesy_get_parent(struct jesy_context *ctx)
{
  if ((ctx) && HAS_PARENT(ctx->iter)) {
    ctx->iter = &ctx->pool[ctx->iter->parent];
    return ctx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

struct jessy_element jesy_get_child(struct jesy_context *ctx)
{
  if ((ctx) && HAS_CHILD(ctx->iter)) {
    ctx->iter = &ctx->pool[ctx->iter->child];
    return ctx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

struct jessy_element jesy_get_next(struct jesy_context *ctx)
{
  if ((ctx) && HAS_RIGHT(ctx->iter)) {
    ctx->iter = &ctx->pool[ctx->iter->right];
    return ctx->iter->data;
  }
  return (struct jessy_element){ 0 };
}

void jesy_reset_iterator(struct jesy_context *ctx)
{
  ctx->iter = ctx->root;
}

bool jesy_find(struct jesy_context *ctx, char *key)
{
  bool result = false;
  struct jesy_node *iter = ctx->iter;
  uint16_t key_length = (uint16_t)strnlen(key, 0xFFFF);
  if ((key_length == 0) || (key_length == 0xFFFF)) {
    return false;
  }

  if (iter->data.type != JESY_OBJECT) {
    if (!(iter = jesy_get_parent_node_bytype(ctx, iter, JESY_OBJECT))) {
      return false;
    }
  }

  iter = jesy_get_child_node(ctx, iter);
  assert(iter);
  assert(iter->data.type == JESY_KEY);

  while (iter) {
    if ((iter->data.length == key_length) &&
        (memcmp(iter->data.value, key, key_length) == 0)) {
      ctx->iter = iter;
      result = true;
      break;
    }
    iter = jesy_get_right_node(ctx, iter);
  }
  return result;
}

bool jesy_has(struct jesy_context *ctx, char *key)
{
  bool result = false;
  struct jesy_node *iter = ctx->iter;
  uint16_t key_length = (uint16_t)strnlen(key, 0xFFFF);
  if ((key_length == 0) || (key_length == 0xFFFF)) {
    return false;
  }

  if (iter->data.type != JESY_OBJECT) {
    if (!(iter = jesy_get_parent_node_bytype(ctx, iter, JESY_OBJECT))) {
      return false;
    }
  }

  iter = jesy_get_child_node(ctx, iter);
  assert(iter);
  assert(iter->data.type == JESY_KEY);

  while (iter) {
    if ((iter->data.length == key_length) &&
        (memcmp(iter->data.value, key, key_length) == 0)) {
      result = true;
      break;
    }
    iter = jesy_get_right_node(ctx, iter);
  }
  return result;
}


enum jesy_node_type jesy_get_type(struct jesy_context *ctx, char *key)
{
  enum jesy_node_type result = JESY_NONE;
  struct jesy_node *iter = ctx->iter;
  uint16_t key_length = (uint16_t)strnlen(key, 0xFFFF);
  if ((key_length == 0) || (key_length == 0xFFFF)) {
    return false;
  }

  if (iter->data.type != JESY_OBJECT) {
    if (!(iter = jesy_get_parent_node_bytype(ctx, iter, JESY_OBJECT))) {
      return false;
    }
  }

  iter = jesy_get_child_node(ctx, iter);
  assert(iter);
  assert(iter->data.type == JESY_KEY);

  while (iter) {
    if ((iter->data.length == key_length) &&
        (memcmp(iter->data.value, key, key_length) == 0)) {
      if (HAS_CHILD(iter)) {
          result = jesy_get_child_node(ctx, iter)->data.type;
          break;
      }
    }
    iter = jesy_get_right_node(ctx, iter);
  }
  return result;
}

bool jesy_set(struct jesy_context *ctx, char *key, char *value, uint16_t length)
{




}