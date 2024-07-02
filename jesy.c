#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <assert.h>
#include "jesy.h"

#ifdef JESY_USE_32BIT_NODE_DESCRIPTOR
  #define JESY_INVALID_INDEX 0xFFFFFFFF
  #define JESY_MAX_VALUE_LEN 0xFFFFFFFF
#else
  #define JESY_INVALID_INDEX 0xFFFF
  #define JESY_MAX_VALUE_LEN 0xFFFF
#endif

#define JESY_ARRAY_LEN(arr) (sizeof(arr)/sizeof(arr[0]))
#define UPDATE_TOKEN(tok, type_, offset_, size_) \
  tok.type = type_; \
  tok.offset = offset_; \
  tok.length = size_;

#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))
#define LOOK_AHEAD(ctx_) (((ctx_->offset + 1) < ctx_->json_size) ? ctx_->json_data[ctx_->offset + 1] : '\0')

#define HAS_PARENT(node_ptr) (node_ptr->parent < JESY_INVALID_INDEX)
#define HAS_SIBLING(node_ptr) (node_ptr->sibling < JESY_INVALID_INDEX)
#define HAS_CHILD(node_ptr) (node_ptr->first_child < JESY_INVALID_INDEX)

#define GET_PARENT(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? &ctx_->pool[node_ptr->parent] : NULL)
#define GET_SIBLING(ctx_, node_ptr) (HAS_SIBLING(node_ptr) ? &ctx_->pool[node_ptr->sibling] : NULL)
#define GET_CHILD(ctx_, node_ptr) (HAS_CHILD(node_ptr) ? &ctx_->pool[node_ptr->first_child] : NULL)

#define PARENT_TYPE(ctx_, node_ptr) (HAS_PARENT(node_ptr) ? ctx_->pool[node_ptr->parent].type : JESY_NONE)

static struct jesy_element *jesy_find_duplicate_key(struct jesy_context *ctx,
                                                    struct jesy_element *object_node,
                                                    struct jesy_token *key_token);

static struct jesy_element* jesy_allocate(struct jesy_context *ctx)
{
  struct jesy_element *new_element = NULL;

  if (ctx->node_count < ctx->capacity) {
    if (ctx->free) {
      /* Pop the first node from free list */
      new_element = (struct jesy_element*)ctx->free;
      ctx->free = ctx->free->next;
    }
    else {
      assert(ctx->index < ctx->capacity);
      new_element = &ctx->pool[ctx->index];
      ctx->index++;
    }
    /* Setting node descriptors to their default values. */
    memset(&new_element->parent, 0xFF, sizeof(jesy_node_descriptor) * 4);
    ctx->node_count++;
  }
  else {
    ctx->status = JESY_OUT_OF_MEMORY;
  }

  return new_element;
}

static void jesy_free(struct jesy_context *ctx, struct jesy_element *element)
{
  struct jesy_free_node *free_node = (struct jesy_free_node*)element;

  assert(element >= ctx->pool);
  assert(element < (ctx->pool + ctx->capacity));
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

static bool jesy_validate_element(struct jesy_context *ctx, struct jesy_element *element)
{
  assert(ctx);
  assert(element);

  if ((element >= ctx->pool) &&
      ((((void*)element - (void*)ctx->pool) % sizeof(*element)) == 0) &&
      ((element >= ctx->pool) < ctx->capacity)) {
    return true;
  }

  return false;
}

struct jesy_element* jesy_get_parent(struct jesy_context *ctx, struct jesy_element *element)
{
  if (ctx && element && jesy_validate_element(ctx, element)) {
    if (HAS_PARENT(element)) {
      return &ctx->pool[element->parent];
    }
  }
  return NULL;
}

struct jesy_element* jesy_get_sibling(struct jesy_context *ctx, struct jesy_element *element)
{
  if (ctx && element && jesy_validate_element(ctx, element)) {
    if (HAS_SIBLING(element)) {
      return &ctx->pool[element->sibling];
    }
  }
  return NULL;
}

struct jesy_element* jesy_get_child(struct jesy_context *ctx, struct jesy_element *element)
{
  if (ctx && element && jesy_validate_element(ctx, element)) {
    if (HAS_CHILD(element)) {
      return &ctx->pool[element->first_child];
    }
  }
  return NULL;
}

static struct jesy_element* jesy_get_parent_bytype(struct jesy_context *ctx,
                                                   struct jesy_element *element,
                                                   enum jesy_type type)
{
  struct jesy_element *parent = NULL;
  if (ctx && element && jesy_validate_element(ctx, element)) {
    while (element && HAS_PARENT(element)) {
      element = &ctx->pool[element->parent];
      if (element->type == type) {
        parent = element;
        break;
      }
    }
  }
  return parent;
}

static struct jesy_element* jesy_get_structure_parent_node(struct jesy_context *ctx,
                                                           struct jesy_element *element)
{
  struct jesy_element *parent = NULL;
  if (ctx && element && jesy_validate_element(ctx, element)) {
    while (element && HAS_PARENT(element)) {
      element = &ctx->pool[element->parent];
      if ((element->type == JESY_OBJECT) || (element->type == JESY_ARRAY)) {
        parent = element;
        break;
      }
    }
  }
  return parent;
}

static struct jesy_element* jesy_append_element(struct jesy_context *ctx,
                                                struct jesy_element *parent,
                                                uint16_t type,
                                                uint16_t length,
                                                char *value)
{
  struct jesy_element *new_element = jesy_allocate(ctx);

  if (new_element) {
    new_element->type = type;
    new_element->length = length;
    new_element->value = value;

    if (parent) {
      new_element->parent = (jesy_node_descriptor)(parent - ctx->pool); /* parent's index */

      if (HAS_CHILD(parent)) {
        struct jesy_element *last = &ctx->pool[parent->last_child];
        last->sibling = (jesy_node_descriptor)(new_element - ctx->pool); /* new_element's index */
      }
      else {
        parent->first_child = (jesy_node_descriptor)(new_element - ctx->pool); /* new_element's index */
      }
      parent->last_child = (jesy_node_descriptor)(new_element - ctx->pool); /* new_element's index */
    }
    else {
      assert(!ctx->root);
      ctx->root = new_element;
    }
  }

  return new_element;
}

void jesy_delete_element(struct jesy_context *ctx, struct jesy_element *element)
{
  struct jesy_element *iter = element;

  if (!jesy_validate_element(ctx, element)) {
    return;
  }

  while (true) {

    while (HAS_CHILD(iter)) {
      iter = &ctx->pool[iter->first_child];
    }

    if (HAS_PARENT(iter)) {
      ctx->pool[iter->parent].first_child = iter->sibling;
    }

    jesy_free(ctx, iter);
    if (iter == element) {
      break;
    }

    iter = &ctx->pool[iter->parent];
  }
}

const struct {
  char symbol;
  enum jesy_token_type token_type;
} jesy_symbolic_token_mapping[] = {
  {'\0', JESY_TOKEN_EOF             },
  {'{',  JESY_TOKEN_OPENING_BRACKET },
  {'}',  JESY_TOKEN_CLOSING_BRACKET },
  {'[',  JESY_TOKEN_OPENING_BRACE   },
  {']',  JESY_TOKEN_CLOSING_BRACE   },
  {':',  JESY_TOKEN_COLON           },
  {',',  JESY_TOKEN_COMMA           },
  };

static inline bool jesy_get_symbolic_token(struct jesy_context *ctx,
                                           char ch, struct jesy_token *token)
{
  uint32_t idx;
  for (idx = 0; idx < JESY_ARRAY_LEN(jesy_symbolic_token_mapping); idx++) {
    if (ch == jesy_symbolic_token_mapping[idx].symbol) {
      UPDATE_TOKEN((*token), jesy_symbolic_token_mapping[idx].token_type, ctx->offset, 1);
      return true;
    }
  }
  return false;
}

static inline bool jesy_is_symbolic_token(struct jesy_context *ctx,
                                          char ch)
{
  uint32_t idx;
  for (idx = 0; idx < JESY_ARRAY_LEN(jesy_symbolic_token_mapping); idx++) {
    if (ch == jesy_symbolic_token_mapping[idx].symbol) {
      return true;
    }
  }
  return false;
}

static inline bool jesy_get_number_token(struct jesy_context *ctx,
                                         char ch, struct jesy_token *token)
{
  bool tokenizing_completed = false;
  if (IS_DIGIT(ch)) {
    token->length++;
    ch = LOOK_AHEAD(ctx);
    if (!IS_DIGIT(ch) && (ch != '.')) { /* TODO: more symbols are acceptable in the middle of a number */
      tokenizing_completed = true;
    }
  }
  else if (ch == '.') {
    token->length++;
    if (!IS_DIGIT(LOOK_AHEAD(ctx))) {
      token->type = JESY_TOKEN_INVALID;
      tokenizing_completed = true;
    }
  }
  else if (IS_SPACE(ch)) {
    tokenizing_completed = true;
  }
  else {
    token->type = JESY_TOKEN_INVALID;
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

static inline bool jesy_get_specific_token(struct jesy_context *ctx,
                          struct jesy_token *token, char *cmp_str, uint16_t len)
{
  bool tokenizing_completed = false;
  token->length++;
  if (token->length == len) {
    if (0 != (strncmp(&ctx->json_data[token->offset], cmp_str, len))) {
      token->type = JESY_TOKEN_INVALID;
    }
    tokenizing_completed = true;
  }
  return tokenizing_completed;
}

static struct jesy_token jesy_get_token(struct jesy_context *ctx)
{
  struct jesy_token token = { 0 };

  while (true) {

    if ((++ctx->offset >= ctx->json_size) || (ctx->json_data[ctx->offset] == '\0')) {
      /* End of data. If token is incomplete, mark it as invalid. */
      if (token.type) {
        token.type = JESY_TOKEN_INVALID;
      }
      break;
    }

    char ch = ctx->json_data[ctx->offset];

    if (!token.type) {

      if (jesy_get_symbolic_token(ctx, ch, &token)) {
        break;
      }

      if (ch == '\"') {
        /* '\"' won't be a part of token. Use offset of next symbol */
        UPDATE_TOKEN(token, JESY_TOKEN_STRING, ctx->offset + 1, 0);
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JESY_TOKEN_NUMBER, ctx->offset, 1);
        /* Unlike STRINGs, NUMBERs do not have dedicated symbols to indicate the
           end of data. To avoid consuming non-NUMBER characters, take a look ahead
           and stop the process in case of non-numeric symbols. */
        if (jesy_is_symbolic_token(ctx, LOOK_AHEAD(ctx))) {
          break;
        }
        continue;
      }

      if ((ch == '-') && IS_DIGIT(LOOK_AHEAD(ctx))) {
        UPDATE_TOKEN(token, JESY_TOKEN_NUMBER, ctx->offset, 1);
        continue;
      }

      if (ch == 't') {
        UPDATE_TOKEN(token, JESY_TOKEN_TRUE, ctx->offset, 1);
        continue;
      }

      if (ch == 'f') {
        UPDATE_TOKEN(token, JESY_TOKEN_FALSE, ctx->offset, 1);
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
      if (ch == '\"') { /* End of STRING. '\"' symbol isn't a part of token. */
        break;
      }
      /* TODO: add checking for scape symbols */
      token.length++;
      continue;
    }
    else if (token.type == JESY_TOKEN_NUMBER) {
      if (jesy_get_number_token(ctx, ch, &token)) {
        break;
      }
      continue;
    }
    else if (token.type == JESY_TOKEN_TRUE) {
      if (jesy_get_specific_token(ctx, &token, "true", sizeof("true") - 1)) {
        break;
      }
      continue;
    }
    else if (token.type == JESY_TOKEN_FALSE) {
      if (jesy_get_specific_token(ctx, &token, "false", sizeof("false") - 1)) {
        break;
      }
      continue;
    }
    else if (token.type == JESY_TOKEN_NULL) {
      if (jesy_get_specific_token(ctx, &token, "null", sizeof("null") - 1)) {
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

static struct jesy_element *jesy_find_duplicate_key(struct jesy_context *ctx,
                                                    struct jesy_element *object,
                                                    struct jesy_token *key_token)
{
  struct jesy_element *duplicate = NULL;
  struct jesy_element *iter = NULL;

  assert(object->type == JESY_OBJECT);
  if (object->type != JESY_OBJECT) {
    return NULL;
  }

  iter = HAS_CHILD(object) ? &ctx->pool[object->first_child] : NULL;
  while(iter) {
    assert(iter->type == JESY_KEY);
    if ((iter->length == key_token->length) &&
        (strncmp(iter->value, &ctx->json_data[key_token->offset], key_token->length) == 0)) {
      duplicate = iter;
      break;
    }
    iter = HAS_SIBLING(object) ? &ctx->pool[object->sibling] : NULL;
  }
  return duplicate;
}

static bool jesy_accept(struct jesy_context *ctx,
                        enum jesy_token_type token_type,
                        enum jesy_type element_type)
{
  if (ctx->token.type == token_type) {
    struct jesy_element *new_node = NULL;

    if (element_type == JESY_KEY) {
#ifndef JESY_ALLOW_DUPLICATE_KEYS
      /* No duplicate keys in the same object are allowed.
         Only the last key:value will be reported if the keys are duplicated. */
      struct jesy_element *node = jesy_find_duplicate_key(ctx, ctx->iter, &ctx->token);
      if (node) {
        jesy_delete_element(ctx, jesy_get_child(ctx, node));
        ctx->iter = node;
      }
      else
#endif
      {
        new_node = jesy_append_element(ctx, ctx->iter, element_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);
      }
    }
    else if ((element_type == JESY_OBJECT) ||
             (element_type == JESY_ARRAY)) {
      new_node = jesy_append_element(ctx, ctx->iter, element_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);
    }
    else if (element_type == JESY_STRING) {
      new_node = jesy_append_element(ctx, ctx->iter, element_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);
    }
    else if ((element_type == JESY_NUMBER)  ||
             (element_type == JESY_TRUE)    ||
             (element_type == JESY_FALSE)   ||
             (element_type == JESY_NULL)) {
      new_node = jesy_append_element(ctx, ctx->iter, element_type, ctx->token.length, &ctx->json_data[ctx->token.offset]);
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
        if (ctx->iter->type != JESY_ARRAY) {
          ctx->iter = jesy_get_parent_bytype(ctx, ctx->iter, JESY_ARRAY);
        }
        ctx->iter = jesy_get_structure_parent_node(ctx, ctx->iter);
      }
      else if (token_type == JESY_TOKEN_CLOSING_BRACKET) {
        /* {} (empty object)is a special case that needs no iteration in the
           direction the parent node. */
        if (ctx->iter->type != JESY_OBJECT) {
          ctx->iter = jesy_get_parent_bytype(ctx, ctx->iter, JESY_OBJECT);
        }
        ctx->iter = jesy_get_structure_parent_node(ctx, ctx->iter);
      }
      else if (token_type == JESY_TOKEN_COMMA) {
        if ((ctx->iter->type == JESY_OBJECT) ||
            (ctx->iter->type == JESY_ARRAY)) {
          if (!HAS_CHILD(ctx->iter)) {
            ctx->status = JESY_UNEXPECTED_TOKEN;
          }
        }
        else {
          ctx->iter = jesy_get_structure_parent_node(ctx, ctx->iter);
        }
      }
    }

    if (ctx->status) return true;
    if (new_node) {
      ctx->iter = new_node;
      JESY_LOG_NODE("\n    + ", ctx->iter - ctx->pool, ctx->iter->type, ctx->iter->length, ctx->iter->value,
                    ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
    }

    ctx->token = jesy_get_token(ctx);
    return true;
  }

  return false;
}

static bool jesy_expect(struct jesy_context *ctx,
                        enum jesy_token_type token_type,
                        enum jesy_type element_type)
{
  if (jesy_accept(ctx, token_type, element_type)) {
    return true;
  }
  if (!ctx->status) {
    ctx->status = JESY_UNEXPECTED_TOKEN; /* Keep the first error */
#ifndef NDEBUG
  printf("\nJES.Parser error! Unexpected Token. %s \"%.*s\" expected a %s after %s",
      jesy_token_type_str[ctx->token.type], ctx->token.length,
      &ctx->json_data[ctx->token.offset], jesy_token_type_str[token_type], jesy_node_type_str[ctx->iter->type]);
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
  memset(ctx, 0, sizeof(*ctx));
  ctx->status = JESY_NO_ERR;
  ctx->node_count = 0;

  ctx->json_data = NULL;
  ctx->json_size = 0;
  ctx->offset = (uint32_t)-1;
  ctx->index = 0;
  ctx->pool = (struct jesy_element*)(ctx + 1);
  ctx->pool_size = pool_size - (uint32_t)(sizeof(struct jesy_context));
  ctx->capacity = (ctx->pool_size / sizeof(struct jesy_element)) < JESY_INVALID_INDEX
                 ? (jesy_node_descriptor)(ctx->pool_size / sizeof(struct jesy_element))
                 : JESY_INVALID_INDEX -1;

  ctx->iter = NULL;
  ctx->root = NULL;
  ctx->free = NULL;

#ifndef NDEBUG
  printf("\nallocator capacity is %d nodes", ctx->capacity);
#endif

  return ctx;
}

uint32_t jesy_parse(struct jesy_context *ctx, const char *json_data, uint32_t json_length)
{
  ctx->json_data = json_data;
  ctx->json_size = json_length;

  /* Fetch the first token before entering the state machine. */
  ctx->token = jesy_get_token(ctx);
  /* First node is expected to be an OPENING_BRACKET. */
  if (!jesy_expect(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT)) {
    return ctx->status;
  }

  do {
    if (ctx->token.type == JESY_TOKEN_EOF) { break; }
    switch (ctx->iter->type) {
      /* <OPENING_BRACKET<OBJECT>>: CHOICE { <STRING<KEY>>, <CLOSING_BRACKET> }. */
      case JESY_OBJECT:
        if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACKET, JESY_NONE) ||
            jesy_accept(ctx, JESY_TOKEN_COMMA, JESY_NONE) ) {
          break;
        }

        if (!jesy_expect(ctx, JESY_TOKEN_STRING, JESY_KEY)) {
          break;
        }
        jesy_expect(ctx, JESY_TOKEN_COLON, JESY_NONE);
        break;
      /* <KEY>+<COLON>: CHOICE { <VALUE>, <ARRAY>, <OBJECT> } */
      case JESY_KEY:
        if (jesy_accept(ctx, JESY_TOKEN_STRING, JESY_STRING)   ||
            jesy_accept(ctx, JESY_TOKEN_NUMBER, JESY_NUMBER)   ||
            jesy_accept(ctx, JESY_TOKEN_TRUE, JESY_TRUE)       ||
            jesy_accept(ctx, JESY_TOKEN_FALSE, JESY_FALSE)     ||
            jesy_accept(ctx, JESY_TOKEN_NULL, JESY_NULL)       ||
            jesy_accept(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT)) {
          jesy_accept(ctx, JESY_TOKEN_COMMA, JESY_NONE);
          break;
        }

        jesy_expect(ctx, JESY_TOKEN_OPENING_BRACE, JESY_ARRAY);
        break;
      /* ARRAY: COICE { VALUE, OPENING_BRACE, CLOSING_BRACE, OPENING_BRACKET } */
      case JESY_ARRAY:
        if (jesy_accept(ctx, JESY_TOKEN_STRING, JESY_STRING)  ||
            jesy_accept(ctx, JESY_TOKEN_NUMBER, JESY_NUMBER)  ||
            jesy_accept(ctx, JESY_TOKEN_TRUE, JESY_TRUE)      ||
            jesy_accept(ctx, JESY_TOKEN_FALSE, JESY_FALSE)    ||
            jesy_accept(ctx, JESY_TOKEN_NULL, JESY_NULL)      ||
            jesy_accept(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT) ||
            jesy_accept(ctx, JESY_TOKEN_OPENING_BRACE, JESY_ARRAY)) {
          jesy_accept(ctx, JESY_TOKEN_COMMA, JESY_NONE);
          break;
        }

        if (jesy_accept(ctx, JESY_TOKEN_COMMA, JESY_NONE)) {
          if (jesy_accept(ctx, JESY_TOKEN_STRING, JESY_STRING)  ||
              jesy_accept(ctx, JESY_TOKEN_NUMBER, JESY_NUMBER)  ||
              jesy_accept(ctx, JESY_TOKEN_TRUE, JESY_TRUE)      ||
              jesy_accept(ctx, JESY_TOKEN_FALSE, JESY_FALSE)    ||
              jesy_accept(ctx, JESY_TOKEN_NULL, JESY_NULL)      ||
              jesy_accept(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT)) {
            break;
          }
          jesy_expect(ctx, JESY_TOKEN_OPENING_BRACE, JESY_ARRAY);
          break;
        }

        jesy_expect(ctx, JESY_TOKEN_CLOSING_BRACE, JESY_NONE);
        break;
      /* VALUE: CHOICE { COMMA, CLOSING_BRACE, CLOSING_BRACKET } */
      case JESY_STRING:
      case JESY_NUMBER:
      case JESY_TRUE:
      case JESY_FALSE:
      case JESY_NULL:
        if (HAS_PARENT(ctx->iter)) {
          if (ctx->pool[ctx->iter->parent].type == JESY_KEY) {
            if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACKET, JESY_NONE)) {
              break;
            }
          }
          else if (ctx->pool[ctx->iter->parent].type == JESY_ARRAY) {
            if (jesy_accept(ctx, JESY_TOKEN_CLOSING_BRACE, JESY_NONE)) {
              break;
            }
          }
          else {
            //printf("\n 1.node type: %s, parent type: %s", jesy_node_type_str[ctx->iter->type], jesy_node_type_str[ctx->pool[ctx->iter->parent].type]);
            assert(0);
          }
        }

        jesy_expect(ctx, JESY_TOKEN_COMMA, JESY_NONE);

        if (ctx->iter->type == JESY_KEY) {
        }
        else if (ctx->iter->type == JESY_ARRAY) {
          if (jesy_accept(ctx, JESY_TOKEN_STRING, JESY_STRING)  ||
              jesy_accept(ctx, JESY_TOKEN_NUMBER, JESY_NUMBER)  ||
              jesy_accept(ctx, JESY_TOKEN_TRUE, JESY_TRUE)      ||
              jesy_accept(ctx, JESY_TOKEN_FALSE, JESY_FALSE)    ||
              jesy_accept(ctx, JESY_TOKEN_NULL, JESY_NULL)      ||
              jesy_accept(ctx, JESY_TOKEN_OPENING_BRACKET, JESY_OBJECT) ||
              jesy_expect(ctx, JESY_TOKEN_OPENING_BRACE, JESY_ARRAY)) {
            break;
          }
        }
        else {
          //printf("\n 2.node type: %s, parent type: %s", jesy_node_type_str[ctx->iter->type], jesy_node_type_str[ctx->pool[ctx->iter->parent].type]);
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
enum jesy_state {
  JESY_STATE_NONE,
  JESY_STATE_WANT_OBJECT,
  JESY_STATE_WANT_KEY,
  JESY_STATE_WANT_VALUE,
  JESY_STATE_WANT_ARRAY_VALUE,
  JESY_STATE_GOT_ARRAY_VALUE,
  JESY_STATE_GOT_VALUE,
  JESY_STATE_GOT_KEY,
};

size_t jesy_evaluate(struct jesy_context *ctx)
{
  size_t json_len = 0;
  enum jesy_state state = JESY_STATE_WANT_OBJECT;
  ctx->status = JESY_NO_ERR;

  if (!ctx->root) {
    return 0;
  }

  ctx->iter = ctx->root;

  do {
    JESY_LOG_NODE("\n   ", ctx->iter - ctx->pool, ctx->iter->type,ctx->iter->length, ctx->iter->value,
                  ctx->iter->parent, ctx->iter->sibling, ctx->iter->first_child, "");
    switch (state) {
      case JESY_STATE_WANT_OBJECT:
        if (ctx->iter->type == JESY_OBJECT) {
          json_len++; /* '{' */
          state = JESY_STATE_WANT_KEY;
        }
        else {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.1");
          return 0;
        }
        break;

      case JESY_STATE_WANT_KEY:
        if (ctx->iter->type == JESY_KEY) {
          json_len += (ctx->iter->length + sizeof(char) * 3);/* +1 for ':' +2 for "" */
          state = JESY_STATE_WANT_VALUE;
        }
        else {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.2");
          return 0;
        }
        break;

      case JESY_STATE_WANT_VALUE:
        if (ctx->iter->type == JESY_STRING) {
            json_len += (ctx->iter->length + sizeof(char) * 2);/* +2 for "" */
            state = JESY_STATE_GOT_VALUE;
        }
        else if ((ctx->iter->type == JESY_NUMBER)  ||
                 (ctx->iter->type == JESY_TRUE)    ||
                 (ctx->iter->type == JESY_FALSE)   ||
                 (ctx->iter->type == JESY_NULL)) {
          json_len += ctx->iter->length;
          state = JESY_STATE_GOT_VALUE;
        }
        else if (ctx->iter->type == JESY_ARRAY) {
          json_len++; /* '[' */
          state = JESY_STATE_WANT_ARRAY_VALUE;
        }
        else if (ctx->iter->type == JESY_OBJECT) {
          json_len++; /* '{' */
          state = JESY_STATE_WANT_KEY;
        }
        else {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.3");
          return 0;
        }
        break;

      case JESY_STATE_WANT_ARRAY_VALUE:
        if (ctx->iter->type == JESY_STRING) {
            json_len += (size_t)ctx->iter->length + (sizeof("\"\"") - 1);
        }
        else if ((ctx->iter->type == JESY_NUMBER)  ||
                 (ctx->iter->type == JESY_TRUE)    ||
                 (ctx->iter->type == JESY_FALSE)   ||
                 (ctx->iter->type == JESY_NULL)) {
          json_len += ctx->iter->length;
        }
        else if (ctx->iter->type == JESY_ARRAY) {
          json_len++; /* '[' */
          state = JESY_STATE_WANT_ARRAY_VALUE;
        }
        else if (ctx->iter->type == JESY_OBJECT) {
          json_len++; /* '{' */
          state = JESY_STATE_WANT_KEY;
        }
        else {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.4");
          return 0;
        }
        break;

      default:
        assert(0);
        break;
    }

    if (HAS_CHILD(ctx->iter)) {
      ctx->iter = jesy_get_child(ctx, ctx->iter);
      continue;
    }

    /* This covers empty objects */
    if (ctx->iter->type == JESY_OBJECT) {
      json_len++; /* '}' */
    }
    /* This covers empty arrays */
    else if (ctx->iter->type == JESY_ARRAY) {
      json_len++; /* ']' */
    }

    /* We've got an array */
    if (HAS_SIBLING(ctx->iter)) {
      if (ctx->iter->type != JESY_KEY) {
        ctx->iter = &ctx->pool[ctx->iter->sibling];
        json_len++; /* ',' */
        continue;
      }
      else {
        ctx->status = JESY_UNEXPECTED_NODE;
        JESY_LOG_MSG("\n jesy_evaluate err.5");
        return 0;
      }
    }

    while (HAS_PARENT(ctx->iter)) {
      /* A key without value is invalid. */
      if (PARENT_TYPE(ctx, ctx->iter) == JESY_KEY) {
        state = JESY_STATE_GOT_VALUE;
      }

      ctx->iter = GET_PARENT(ctx, ctx->iter);
      if (ctx->iter->type == JESY_KEY) {
        if (state != JESY_STATE_GOT_VALUE) {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.6");
          return 0;
        }
        state = JESY_STATE_GOT_KEY;
      }
      else if (ctx->iter->type == JESY_OBJECT) {
        if (state != JESY_STATE_GOT_KEY) {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.7");
          return 0;
        }
        json_len++; /* '}' */
      }
      else if (ctx->iter->type == JESY_ARRAY) {
        json_len++; /* ']' */
      }

      if (HAS_SIBLING(ctx->iter)) {
        json_len++; /* ',' */
        if (PARENT_TYPE(ctx, ctx->iter) == JESY_OBJECT) {
          state = JESY_STATE_WANT_KEY;
        }
        else if (PARENT_TYPE(ctx, ctx->iter) == JESY_ARRAY) {
          state = JESY_STATE_WANT_ARRAY_VALUE;
        }
        else {
          ctx->status = JESY_UNEXPECTED_NODE;
          JESY_LOG_MSG("\n jesy_evaluate err.8");
          return 0;
        }
        ctx->iter = GET_SIBLING(ctx, ctx->iter);
        break;
      }
    }
  } while (ctx->iter != ctx->root);

  ctx->iter = ctx->root;
  return json_len;
}

uint32_t jesy_render(struct jesy_context *ctx, char *buffer, uint32_t length)
{
  char *dst = buffer;
  struct jesy_element *iter = ctx->root;
  uint32_t required_buffer = 0;

  required_buffer = jesy_evaluate(ctx);
  if (length < required_buffer) {
    ctx->status = JESY_OUT_OF_MEMORY;
    return 0;
  }
  if (required_buffer == 0) {
    return 0;
  }
  buffer[0] = '\0';

  while (iter) {

    if (iter->type == JESY_OBJECT) {
      *dst++ = '{';
    }
    else if (iter->type == JESY_KEY) {
      *dst++ = '"';
      dst = (char*)memcpy(dst, iter->value, iter->length) + iter->length;
      *dst++ = '"';
      *dst++ = ':';
    }
    else if (iter->type == JESY_STRING) {
      *dst++ = '"';
      dst = (char*)memcpy(dst, iter->value, iter->length) + iter->length;
      *dst++ = '"';
    }
    else if ((iter->type == JESY_NUMBER)  ||
             (iter->type == JESY_TRUE)    ||
             (iter->type == JESY_FALSE)   ||
             (iter->type == JESY_NULL)) {
      dst = (char*)memcpy(dst, iter->value, iter->length) + iter->length;
    }
    else if (iter->type == JESY_ARRAY) {
      *dst++ = '[';
    }
    else {
      assert(0);
      ctx->status = JESY_UNEXPECTED_NODE;
      break;
    }

    if (HAS_CHILD(iter)) {
      iter = jesy_get_child(ctx, iter);
      continue;
    }

    if (iter->type == JESY_OBJECT) {
      *dst++ = '}';
    }

    else if (iter->type == JESY_ARRAY) {
      *dst++ = ']';
    }

    if (HAS_SIBLING(iter)) {
      iter = jesy_get_sibling(ctx, iter);
      *dst++ = ',';
      continue;
    }

     while ((iter = jesy_get_parent(ctx, iter))) {
      if (iter->type == JESY_OBJECT) {
        *dst++ = '}';
      }
      else if (iter->type == JESY_ARRAY) {
        *dst++ = ']';
      }
      if (HAS_SIBLING(iter)) {
        iter = jesy_get_sibling(ctx, iter);
        *dst++ = ',';
        break;
      }
    }
  }

  ctx->iter = ctx->root;
  return dst - buffer;
}

struct jesy_element* jesy_get_root(struct jesy_context *ctx)
{
  if (ctx) {
    return ctx->root;
  }
  return NULL;
}

struct jesy_element* jesy_get_key(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  struct jesy_element *key_element = NULL;
  struct jesy_element *iter = NULL;
  uint32_t key_len;
  char *dot;

  if (ctx && object && keys && jesy_validate_element(ctx, object)) {
    if (object->type != JESY_OBJECT) {
      return NULL;
    }
    while (dot = strchr(keys, '.')) {
      key_len = dot - keys;
      iter = GET_CHILD(ctx, object);
      while (iter) {
        if ((iter->length == key_len) && (0 == memcmp(iter->value, keys, key_len))) {
          if (iter->type == JESY_KEY) {
            object = GET_CHILD(ctx, iter);
            if (object->type != JESY_OBJECT) {
              return NULL;
            }
          }
          break;
        }
        iter = GET_SIBLING(ctx, iter);
      }
      keys = keys + key_len + sizeof(*dot);
    }
    key_len = strlen(keys);
    iter = GET_CHILD(ctx, object);
    while (iter) {
      if ((iter->length == key_len) && (0 == memcmp(iter->value, keys, key_len))) {
        if (iter->type == JESY_KEY) {
          key_element = iter;
        }
        break;
      }
      iter = GET_SIBLING(ctx, iter);
    }
  }
  return key_element;
}

struct jesy_element* jesy_get_key_value(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  struct jesy_element *value_element = NULL;
  struct jesy_element *key = jesy_get_key(ctx, object, keys);
  if (key) {
    value_element = GET_CHILD(ctx, key);
  }
  return value_element;
}

struct jesy_element* jesy_get_object(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  struct jesy_element *value_element = jesy_get_object(ctx, object, keys);
  if (value_element && value_element->type == JESY_OBJECT) {
    return value_element;
  }
  return NULL;
}

struct jesy_element* jesy_get_array(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  struct jesy_element *value_element = jesy_get_object(ctx, object, keys);
  if (value_element && value_element->type == JESY_ARRAY) {
    return value_element;
  }
  return NULL;
}

struct jesy_element* jesy_get_array_value(struct jesy_context *ctx, struct jesy_element *array, int16_t index)
{
  struct jesy_element *iter = NULL;
  if (ctx && array && jesy_validate_element(ctx, array)) {
    if (array->type != JESY_ARRAY) {
      return NULL;
    }

    if (index >= 0) {
      iter = HAS_CHILD(array) ? &ctx->pool[array->first_child] : NULL;
      for (; iter && index > 0; index--) {
        iter = HAS_SIBLING(iter) ? &ctx->pool[iter->sibling] : NULL;
      }
    }
  }
  return iter;
}

struct jesy_element* jesy_add_element(struct jesy_context *ctx, struct jesy_element *parent, enum jesy_type type, char *value)
{
  size_t length;
  if (!ctx) {
    ctx->status = JESY_INVALID_PARAMETER;
    return NULL;
  }

  if (!parent && ctx->root) { /* JSON is not empty. Invalid request. */
    ctx->status = JESY_INVALID_PARAMETER;
    return NULL;
  }

  if (parent && !jesy_validate_element(ctx, parent)) {
    ctx->status = JESY_INVALID_PARAMETER;
    return NULL;
  }

  length = strlen(value);
  if (length > 65535) {
    return NULL;
  }

  return jesy_append_element(ctx, parent, type, strlen(value), value);
}

struct jesy_element* jesy_add_key(struct jesy_context *ctx, struct jesy_element *object, char *keyword)
{
  return jesy_add_element(ctx, object, JESY_KEY, keyword);
}

uint32_t jesy_update_key(struct jesy_context *ctx, struct jesy_element *key, char *keyword)
{
  uint32_t result = JESY_INVALID_PARAMETER;

  if (ctx && key && jesy_validate_element(ctx, key)) {
    if (key->type == JESY_KEY) {
      size_t key_len = strlen(keyword);
      if (key_len < 65535) {
        key->length = key_len;
        key->value = keyword;
        result = JESY_NO_ERR;
      }
    }
  }
  return result;
}

uint32_t jesy_update_key_value(struct jesy_context *ctx, struct jesy_element *object, char *keys, enum jesy_type type, char *value)
{
  uint32_t result = JESY_INVALID_PARAMETER;

  struct jesy_element *key = jesy_get_key(ctx, object, keys);
  if (key) {
    jesy_delete_element(ctx, GET_CHILD(ctx, key));
    if (jesy_add_element(ctx, key, type, value)) {
      result = JESY_NO_ERR;
    }
    else {
      result = ctx->status;
    }
  }
  return result;
}

uint32_t jesy_update_key_value_object(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  return jesy_update_key_value(ctx, object, keys, JESY_OBJECT, "");
}

uint32_t jesy_update_key_value_array(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  return jesy_update_key_value(ctx, object, keys, JESY_ARRAY, "");
}

uint32_t jesy_update_key_value_true(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  return jesy_update_key_value(ctx, object, keys, JESY_TRUE, "");
}

uint32_t jesy_update_key_value_false(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  return jesy_update_key_value(ctx, object, keys, JESY_FALSE, "");
}

uint32_t jesy_update_key_value_null(struct jesy_context *ctx, struct jesy_element *object, char *keys)
{
  return jesy_update_key_value(ctx, object, keys, JESY_NULL, "");
}

uint32_t jesy_update_array_value(struct jesy_context *ctx, struct jesy_element *array, int16_t index, enum jesy_type type, char *value)
{
  uint32_t result = JESY_ELEMENT_NOT_FOUND;
  struct jesy_element *value_element = jesy_get_array_value(ctx, array, index);
  if (value_element) {
    while (HAS_CHILD(value_element)) {
      jesy_delete_element(ctx, GET_CHILD(ctx, value_element));
    }
    value_element->type = type;
    value_element->length = (uint16_t)strnlen(value, 0xFFFF);
    value_element->value = value;
    result = JESY_NO_ERR;
  }
  return result;
}