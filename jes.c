
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
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

static char token_type_str[][20] = {
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

enum jes_token_type {
  JES_EOF = 0,
  JES_BRACKET_OPEN,
  JES_BRACKET_CLOSE,
  JES_BRACE_OPEN,
  JES_BRACE_CLOSE,
  JES_STRING,
  JES_NUMBER,
  JES_BOOLEAN,
  JES_NULL,
  JES_COLON,
  JES_COMMA,
  JES_ESC,
  JES_INVALID,
};

enum jes_node_type {
  JES_NONE = 0,
  JES_OBJECT,
  JES_KEY,
  JES_VALUE
};
struct jes_value {
  uint32_t offset;
  uint32_t length;
  struct jes_value *next;
};

struct jes_key {
  uint32_t offset;
  uint32_t length;
  struct jes_value *value;
};

struct jes_object {
  struct key *keys;
};

enum jes_value_type {
  JES_UNKOWN = 0,
  JES_STRING_,
  JES_NUMBER_,
};

struct jes_token {
  enum jes_token_type type;
  uint32_t offset;
  uint32_t size;
};

struct jes_parser_context {
  uint8_t   *json_data;
  uint32_t  size;
  int32_t   offset;
  struct jes_token token;
  void *mem_pool;
  uint32_t mem_calc;
  uint32_t element_count;
  struct jes_object *head;
  void *node;
};

/* Function Prototypes */
static bool jes_parse_object(struct jes_parser_context *);
static bool jes_parse_value(struct jes_parser_context *);
static bool jes_parse_array(struct jes_parser_context *);

static void jes_log(struct jes_parser_context *ctx, const struct jes_token *token)
{
  printf("\n    eJSON::Token: [Pos: %5d, Len: %3d] %s \"%.*s\"",
          token->offset, token->size, token_type_str[token->type],
          token->size, &ctx->json_data[token->offset]);
}

void jes_init_context(struct jes_parser_context *ctx, unsigned char *buffer)
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

static struct jes_token get_token(struct jes_parser_context *ctx)
{
  struct jes_token token = { 0 };

  while (true) {

    if (++ctx->offset >= ctx->size) {
      /* End of buffer.
         If there is a token in process, mark it as invalid. */
      if (token.type) {
        token.type = JES_INVALID;
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
        UPDATE_TOKEN(token, JES_BRACKET_OPEN, ctx->offset, 1);
        break;
      }

      if (ch == '}') {
        UPDATE_TOKEN(token, JES_BRACKET_CLOSE, ctx->offset, 1);
        break;
      }

      if (ch == '[') {
        UPDATE_TOKEN(token, JES_BRACE_OPEN, ctx->offset, 1);
        break;
      }

      if (ch == ']') {
        UPDATE_TOKEN(token, JES_BRACE_CLOSE, ctx->offset, 1);
        break;
      }

      if (ch == ':') {
        UPDATE_TOKEN(token, JES_COLON, ctx->offset, 1)
        break;
      }

      if (ch == ',') {
        UPDATE_TOKEN(token, JES_COMMA, ctx->offset, 1)
        break;
      }

      if (ch == '\"') {
        /* Use the next offset since '\"' won't be a part of token. */
        UPDATE_TOKEN(token, JES_STRING, ctx->offset + 1, 0);
        if (IS_EOF_AHEAD(ctx)) {
          UPDATE_TOKEN(token, JES_INVALID, ctx->offset, 1);
          break;
        }
        continue;
      }

      if (IS_DIGIT(ch)) {
        UPDATE_TOKEN(token, JES_NUMBER, ctx->offset, 1);
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
          UPDATE_TOKEN(token, JES_NUMBER, ctx->offset, 1);
          continue;
        }
        UPDATE_TOKEN(token, JES_INVALID, ctx->offset, 1);
        break;
      }

      if ((ch == 't') || (ch == 'f')) {
        if ((LOOK_AHEAD(ctx) < 'a') || (LOOK_AHEAD(ctx) > 'z')) {
          UPDATE_TOKEN(token, JES_INVALID, ctx->offset, 1);
          break;
        }
        UPDATE_TOKEN(token, JES_BOOLEAN, ctx->offset, 1);
        continue;
      }

      if (ch == 'n') {
        UPDATE_TOKEN(token, JES_NULL, ctx->offset, 1);
        continue;
      }

      /* Skipping space symbols including: space, tab, carriage return */
      if (IS_SPACE(ch)) {
        continue;
      }

      UPDATE_TOKEN(token, JES_INVALID, ctx->offset, 1);
      break;
    }
    else if (token.type == JES_STRING) {

      /* We'll not deliver '\"' symbol as a part of token. */
      if (ch == '\"') {
        break;
      }

      if (ch == '\\') {
        if (LOOK_AHEAD(ctx) != 'n') {
          token.type = JES_INVALID;
          break;
        }
      }

      token.size++;
      continue;
    }
    else if (token.type == JES_NUMBER) {

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
          token.type = JES_INVALID;
          break;
        }
        continue;
      }

      if (IS_SPACE(ch)) {
        break;
      }

      token.type = JES_INVALID;
      break;

    } else if (token.type == JES_BOOLEAN) {
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
        token.type = JES_INVALID;
        break;
      }
      continue;
    } else if (token.type == JES_NULL) {
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
        token.type = JES_INVALID;
        break;
      }
      continue;
    }

    token.type = JES_INVALID;
    break;
  }
  return token;
}

static bool jes_accept(struct jes_parser_context *ctx, enum jes_token_type token_type, enum jes_node_type node_type)
{
  if (ctx->token.type == token_type) {
#ifdef LOG
    jes_log(ctx, &ctx->token);
#endif
    switch (node_type) {
      case JES_OBJECT:
        ctx->mem_calc += sizeof(struct jes_object);
        ctx->element_count++;
#ifdef LOG
        printf("\n        Node of type OBJECT is created:");
#endif
        break;
      case JES_KEY:
        ctx->mem_calc += sizeof(struct jes_key);
        ctx->element_count++;
#ifdef LOG
        printf("\n        Node of type KEY is created:");
#endif
        break;
      case JES_VALUE:
        ctx->mem_calc += sizeof(struct jes_value);
        ctx->element_count++;
#ifdef LOG
        printf("\n        Node of type VALUE is created:");
#endif
        break;
      case JES_NONE:
        break;
      default:

        break;
    }
    ctx->token = get_token(ctx);
    return true;
  }
  return false;
}

static bool jes_expect(struct jes_parser_context *ctx, enum jes_token_type token_type, enum jes_node_type node_type)
{
  if (jes_accept(ctx, token_type, node_type)) {
    return true;
  }
  printf("\neJSON> Parser error! Unexpected Token. expected: %s, got: %s \"%.*s\"",
      token_type_str[token_type], token_type_str[ctx->token.type], ctx->token.size,
      &ctx->json_data[ctx->token.offset]);
  return false;
}

static bool jes_parse_array(struct jes_parser_context *ctx)
{
  if (!jes_accept(ctx, JES_BRACE_OPEN, JES_NONE)) {
    return false;
  }
  do {
    if (jes_parse_value(ctx)) {
    }
  } while (jes_accept(ctx, JES_COMMA, JES_NONE));

  if (!jes_expect(ctx, JES_BRACE_CLOSE, JES_NONE)) {
    return false;
  }
  return true;
}

static bool jes_parse_value(struct jes_parser_context *ctx)
{
  if (jes_accept(ctx, JES_STRING, JES_VALUE)) {
    return true;
  }
  else if (jes_accept(ctx, JES_NUMBER, JES_VALUE)) {
    return true;
  }
  else if (jes_accept(ctx, JES_BOOLEAN, JES_VALUE)) {
    return true;
  }
  else if (jes_accept(ctx, JES_NULL, JES_VALUE)) {
    return true;
  }
  else if (jes_parse_array(ctx)) {
    return true;
  }
  else if (jes_parse_object(ctx)) {
    return true;
  }
  return false;
}

static bool jes_parse_key_value(struct jes_parser_context *ctx)
{
  if (!jes_accept(ctx, JES_STRING, JES_KEY)) {
    return false;
  }
  if (!jes_expect(ctx, JES_COLON, JES_NONE)) {
    return false;
  }

  if(!jes_parse_value(ctx)) {
    return false;
  }

  return true;

}


static bool jes_parse_object(struct jes_parser_context *ctx)
{
  if (jes_accept(ctx, JES_BRACKET_OPEN, JES_OBJECT)) {
    do {
      if (!jes_parse_key_value(ctx)) {
        break;
      }
    } while (jes_accept(ctx, JES_COMMA, JES_NONE));
  }
  return jes_expect(ctx, JES_BRACKET_CLOSE, JES_NONE);
}

int jes_parse(struct jes_parser_context *ctx, char *json_data, uint32_t size)
{
  struct jes_token token;
  ctx->json_data = json_data;
  ctx->size = size;

  ctx->token = get_token(ctx);
  while (ctx->token.type != JES_EOF && ctx->token.type != JES_INVALID) {
    if (!jes_parse_object(ctx)) {
      printf("\neJSON> Parsing failed!");
      break;
    }
  }

  return 0;
}

int main(void)
{
  struct jes_parser_context ctx;
  FILE *fp;
  char file_data[0xFFFFF];
  printf("EmbeddedJSON...");
  printf("\nSize of jes_object: %d bytes", sizeof(struct jes_object));
  printf("\nSize of jes_value: %d bytes", sizeof(struct jes_value));
  printf("\nSize of jes_key: %d bytes", sizeof(struct jes_key));
  printf("\nSize of jes_token: %d bytes", sizeof(struct jes_token));
  printf("\nSize of jes_parser_context: %d bytes", sizeof(struct jes_parser_context));

  jes_init_context(&ctx, 0);
  fp = fopen("test1.json", "rb");

  if (fp != NULL) {
    size_t newLen = fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }

  jes_parse(&ctx, file_data, sizeof(file_data));
  printf("\nSize of JSON data: %d bytes", strnlen(file_data, sizeof(file_data)));
  printf("\nMemory required: %d bytes for %d elements.", ctx.mem_calc, ctx.element_count);
}