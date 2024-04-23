
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdbool.h>
#include <stddef.h>
#include <assert.h>

#define LOG

#define UPDATE_TOKEN(token, type_, offset_, size_) \
  token.type = type_; \
  token.offset = offset_; \
  token.size = size_;

#define LOOK_AHEAD(ctx_) ctx_->json_data[ctx_->offset + 1]
#define IS_EOF_AHEAD(ctx_) (((ctx_->offset + 1) >= ctx_->size) || \
                            (ctx_->json_data[ctx_->offset + 1] == '\0'))
#define IS_SPACE(c) ((c==' ') || (c=='\t') || (c=='\r') || (c=='\n'))
#define IS_DIGIT(c) ((c >= '0') && (c <= '9'))
#define IS_ESCAPE(c) ((c=='\\') || (c=='\"') || (c=='\/') || (c=='\b') || \
                      (c=='\f') || (c=='\n') || (c=='\r') || (c=='\t') || (c == '\u'))

static char type_str[][20] = {
  "EOF          ",
  "BRACKET_OPEN ",
  "BRACKET_CLOSE",
  "BRACE_OPEN   ",
  "BRACE_CLOSE  ",
  "STRING       ",
  "NUMBER       ",
  "BOOLEAN      ",
  "NULL         ",
  "COLON        ",
  "COMMA        ",
  "ESC          ",
  "INVALID      ",
};

enum ej_token_type {
  EJ_EOF = 0,
  EJ_BRACKET_OPEN,
  EJ_BRACKET_CLOSE,
  EJ_BRACE_OPEN,
  EJ_BRACE_CLOSE,
  EJ_STRING,
  EJ_NUMBER,
  EJ_BOOLEAN,
  EJ_NULL,
  EJ_COLON,
  EJ_COMMA,
  EJ_ESC,
  EJ_INVALID,
};

enum ej_node_type {
  EJ_NONE = 0,
  EJ_OBJECT,
  EJ_KEY,
  EJ_VALUE
};
struct ej_value {
  unsigned int start;
  unsigned int length;
  struct ej_value *next;
};

struct ej_key {
  unsigned int start;
  unsigned int length;
  struct ej_value *value;
};

struct ej_object {
  struct key *keys;
};

enum ej_value_type {
  EJ_UNKOWN = 0,
  EJ_STRING_,
  EJ_NUMBER_,
};

struct ej_token {
  enum ej_token_type type;
  int offset;
  int size;
};

struct ej_parser_context {
  char *json_data;
  int  size;
  int  offset;
  struct ej_token token;
  void *mem_pool;
  size_t mem_calc;
  size_t element_count;
  struct ej_object *head;
  void *node;
};

/* Function Prototypes */
static bool ej_parse_object(struct ej_parser_context *);
static bool ej_parse_value(struct ej_parser_context *);
static bool ej_parse_array(struct ej_parser_context *);

static void ej_log(struct ej_parser_context *ctx, const struct ej_token *token)
{
  printf("\n    eJSON::Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token->offset, token->size, type_str[token->type],
          token->size, &ctx->json_data[token->offset]);
}

void ej_init_context(struct ej_parser_context *ctx, unsigned char *buffer)
{
  ctx->json_data = NULL;
  ctx->size = 0;
  ctx->offset = -1;
  ctx->mem_pool = buffer;
  ctx->mem_calc = 0;
  ctx->element_count = 0;
  ctx->node = NULL;
  ctx->head = NULL;
}

static struct ej_token get_token(struct ej_parser_context *ctx)
{
  struct ej_token token = { 0 };

  while (true) {

    if (++ctx->offset >= ctx->size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = EJ_INVALID;
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
        UPDATE_TOKEN(token, EJ_BRACKET_OPEN, ctx->offset, 1);
        break;
      }

      if (ch == '}') {
        UPDATE_TOKEN(token, EJ_BRACKET_CLOSE, ctx->offset, 1);
        break;
      }

      if (ch == '[') {
        UPDATE_TOKEN(token, EJ_BRACE_OPEN, ctx->offset, 1);
        break;
      }

      if (ch == ']') {
        UPDATE_TOKEN(token, EJ_BRACE_CLOSE, ctx->offset, 1);
        break;
      }

      if (ch == ':') {
        UPDATE_TOKEN(token, EJ_COLON, ctx->offset, 1)
        break;
      }

      if (ch == ',') {
        UPDATE_TOKEN(token, EJ_COMMA, ctx->offset, 1)
        break;
      }

      if (ch == '\"') {
        /* Use the next offset since '\"' won't be a part of token. */
        UPDATE_TOKEN(token, EJ_STRING, ctx->offset + 1, 0);
        if (IS_EOF_AHEAD(ctx)) {
          UPDATE_TOKEN(token, EJ_INVALID, ctx->offset, 1);
          break;
        }
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, EJ_NUMBER, ctx->offset, 1);
        /* NUMBERs do not have dedicated enclosing symbols like STRINGs.
           To prevent the tokenizer to consume too much characters, we need to
           look ahead and stop the process if the next character is one of
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
          UPDATE_TOKEN(token, EJ_NUMBER, ctx->offset, 1);
          continue;
        }
        UPDATE_TOKEN(token, EJ_INVALID, ctx->offset, 1);
        break;
      }

      if ((ch == 't') || (ch == 'f')) {
        if ((LOOK_AHEAD(ctx) < 'a') || (LOOK_AHEAD(ctx) > 'z')) {
          UPDATE_TOKEN(token, EJ_INVALID, ctx->offset, 1);
          break;
        }
        UPDATE_TOKEN(token, EJ_BOOLEAN, ctx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, EJ_NULL, ctx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        continue;
      }

      UPDATE_TOKEN(token, EJ_INVALID, ctx->offset, 1);
      break;
    }
    else if (token.type == EJ_STRING) {

      /* We'll not deliver '\"' symbol as a part of token. */
      if (ch == '\"') {
        break;
      }

      if (ch == '\\') {
        if (LOOK_AHEAD(ctx) != 'n') {
          token.type = EJ_INVALID;
          break;
        }
      }

      token.size++;
      continue;
    }
    else if (token.type == EJ_NUMBER) {

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
          token.type = EJ_INVALID;
          break;
        }
        continue;
      }

      if (IS_SPACE(ch)) {
        break;
      }

      token.type = EJ_INVALID;
      break;

    } else if (token.type == EJ_BOOLEAN) {
      token.size++;
      /* Look ahead to find symbols signaling the end of token. */
      if ((LOOK_AHEAD(ctx) == ',') ||
          (LOOK_AHEAD(ctx) == ']') ||
          (LOOK_AHEAD(ctx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(ctx)))) {
        /* Check against "true". Use the longer string as reference. */
        int compare_size = token.size > sizeof("true") - 1
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
        token.type = EJ_INVALID;
        break;
      }
      continue;
    } else if (token.type == EJ_NULL) {
      token.size++;
      /* Look ahead to find symbols signaling the end of token. */
      if ((LOOK_AHEAD(ctx) == ',') ||
          (LOOK_AHEAD(ctx) == ']') ||
          (LOOK_AHEAD(ctx) == '}') ||
          (IS_SPACE(LOOK_AHEAD(ctx)))) {
        /* Check against "null". Use the longer string as reference. */
        int compare_size = token.size > sizeof("null") - 1
                         ? token.size
                         : sizeof("null") - 1;
        if (memcmp("null", &ctx->json_data[token.offset], compare_size) == 0) {
          break;
        }
        token.type = EJ_INVALID;
        break;
      }
      continue;
    }

    token.type = EJ_INVALID;
    break;
  }
  return token;
}

static bool ej_accept(struct ej_parser_context *ctx, enum ej_token_type token_type, enum ej_node_type node_type)
{
  if (ctx->token.type == token_type) {
#ifdef LOG
    ej_log(ctx, &ctx->token);
#endif
    switch (node_type) {
      case EJ_OBJECT:
        ctx->mem_calc += sizeof(struct ej_object);
        ctx->element_count++;
#ifdef LOG
        printf("\n        Node of type OBJECT is created:");
#endif
        break;
      case EJ_KEY:
        ctx->mem_calc += sizeof(struct ej_key);
        ctx->element_count++;
#ifdef LOG
        printf("\n        Node of type KEY is created:");
#endif
        break;
      case EJ_VALUE:
        ctx->mem_calc += sizeof(struct ej_value);
        ctx->element_count++;
#ifdef LOG
        printf("\n        Node of type VALUE is created:");
#endif
        break;
      case EJ_NONE:
        break;
      default:

        break;
    }
    ctx->token = get_token(ctx);
    return true;
  }
  return false;
}

static bool ej_expect(struct ej_parser_context *ctx, enum ej_token_type token_type, enum ej_node_type node_type)
{
  if (ej_accept(ctx, token_type, node_type)) {
    return true;
  }
  printf("\neJSON> Parser error! Unexpected Token. expected: %s, got: %s \"%.*s\"",
      type_str[token_type], type_str[ctx->token.type], ctx->token.size,
      &ctx->json_data[ctx->token.offset]);
  return false;
}

static bool ej_parse_array(struct ej_parser_context *ctx)
{
  if (!ej_accept(ctx, EJ_BRACE_OPEN, EJ_NONE)) {
    return false;
  }
  do {
    if (ej_parse_value(ctx)) {
    }
  } while (ej_accept(ctx, EJ_COMMA, EJ_NONE));

  if (!ej_expect(ctx, EJ_BRACE_CLOSE, EJ_NONE)) {
    return false;
  }
  return true;
}

static bool ej_parse_value(struct ej_parser_context *ctx)
{
  if (ej_accept(ctx, EJ_STRING, EJ_VALUE)) {
    return true;
  }
  else if (ej_accept(ctx, EJ_NUMBER, EJ_VALUE)) {
    return true;
  }
  else if (ej_accept(ctx, EJ_BOOLEAN, EJ_VALUE)) {
    return true;
  }
  else if (ej_accept(ctx, EJ_NULL, EJ_VALUE)) {
    return true;
  }
  else if (ej_parse_array(ctx)) {
    return true;
  }
  else if (ej_parse_object(ctx)) {
    return true;
  }
  return false;
}

static bool ej_parse_key_value(struct ej_parser_context *ctx)
{
  if (!ej_accept(ctx, EJ_STRING, EJ_KEY)) {
    return false;
  }
  if (!ej_expect(ctx, EJ_COLON, EJ_NONE)) {
    return false;
  }

  if(!ej_parse_value(ctx)) {
    return false;
  }

  return true;

}


static bool ej_parse_object(struct ej_parser_context *ctx)
{
  if (ej_accept(ctx, EJ_BRACKET_OPEN, EJ_OBJECT)) {
    do {
      if (!ej_parse_key_value(ctx)) {
        break;
      }
    } while (ej_accept(ctx, EJ_COMMA, EJ_NONE));
  }
  return ej_expect(ctx, EJ_BRACKET_CLOSE, EJ_NONE);
}

int ej_parse(struct ej_parser_context *ctx, char *json_data, int size)
{
  struct ej_token token;
  ctx->json_data = json_data;
  ctx->size = size;
  //printf("\nSource JSON data: %s is %d bytes.\n", json_data, size);
  printf("\nSize of JSON data: %d bytes", strnlen(json_data, size));

#if 0
  do {
    token = get_token(ctx);
#ifdef LOG
    ej_log(ctx, &token);
#endif
    switch (token.type) {
      case EJ_BRACKET_OPEN:
        ctx->mem_calc += sizeof(struct ej_object);
        break;
      case EJ_STRING:
        ctx->mem_calc += sizeof(struct ej_object);
        break;
      case EJ_INVALID:
        printf("\neJSON> Syntax error! Invalid Token \"%.*s\"",
          token.size, &ctx->json_data[token.offset]);
          break;
      default:
        break;
    }
  } while(token.type != EJ_EOF && token.type != EJ_INVALID);
#else
    ctx->token = get_token(ctx);
    while (ctx->token.type != EJ_EOF && ctx->token.type != EJ_INVALID) {
      if (!ej_parse_object(ctx)) {
        printf("\neJSON> Parsing failed!");
        break;
      }
    }


#endif

  return 0;
}

int main(void)
{
  struct ej_parser_context ctx;
  FILE *fp;
  char file_data[0xFFFFF];
  printf("EmbeddedJSON...");
  printf("\nSize of ej_object: %d bytes", sizeof(struct ej_object));
  printf("\nSize of ej_value: %d bytes", sizeof(struct ej_value));
  printf("\nSize of ej_key: %d bytes", sizeof(struct ej_key));
  printf("\nSize of ej_token: %d bytes", sizeof(struct ej_token));
  printf("\nSize of ej_parser_context: %d bytes", sizeof(struct ej_parser_context));

  ej_init_context(&ctx, 0);
  fp = fopen("test1.json", "rb");

  if (fp != NULL) {
    size_t newLen = fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }

  ej_parse(&ctx, file_data, sizeof(file_data));
  printf("\nMemory required: %d bytes for %d elements.", ctx.mem_calc, ctx.element_count);
  //printf("\n is numeric: %d", isnumber('5'));
}