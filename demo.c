
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "jesy.h"
#include "jesy_util.h"

#define POOL_SIZE 0xFFFFFFF
static char file_data[0xFFFFFFF];
static uint8_t mem_pool[POOL_SIZE];
static char output[0xFFFFFFF];


int main(void)
{

  struct jesy_context *ctx;
  FILE *fp;
  size_t out_size;
  jesy_status err;
  struct jesy_element *element;
  struct jesy_element *root;

#if 1
  fp = fopen("test.json", "rb");
#else
  fp = fopen("large.json", "rb");
#endif

  if (fp != NULL) {
    fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }

  ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
  if (!ctx) {
    printf("\n Context initiation failed!");
    return -1;
  }

  printf("\n JESy - Start parsing...");
  if (0 != (err = jesy_parse(ctx, file_data, sizeof(file_data))))
  {
    printf("\n    Parsing Error: %d - %s", err, jesy_status_str[err]);
    return -1;
  }

  printf("\n    Size of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
  printf("\n    JESy node count: %d", ctx->node_count);

  printf("\n JESy: rendering...");
  out_size = jesy_render(ctx, output, sizeof(output));
  if ((out_size == 0) && (ctx->status != 0))
  {
    printf("\n    Render Error: %d - %s", ctx->status, jesy_status_str[ctx->status]);
    printf("\n      \"%.*s\" <%s>", ctx->iter->length, ctx->iter->value, jesy_node_type_str[ctx->iter->type]);
    return -1;
  }

  fp = fopen("result.json", "wb");
  printf("\n    Size of JSON dump:  %d bytes",  out_size);
  if (fp != NULL) {
    fwrite(output, sizeof(char), out_size, fp);
    fclose(fp);
  }

  root = jesy_get_root(ctx);
  element = jesy_get_key_value(ctx, root, "a");
  if (element) {
    printf("\n \"a\": %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]);
    printf("\n ----->Deleting value from \"a\"...");
    jesy_delete_element(ctx, element);
    printf("\n ----->validating...");
    out_size = jesy_validate(ctx);
    if ((out_size == 0) && (ctx->status != 0))
    {
      printf("\n    Validation Error: %d - %s", ctx->status, jesy_status_str[ctx->status]);
      printf("\n      \"%.*s\" <%s>", ctx->iter->length, ctx->iter->value, jesy_node_type_str[ctx->iter->type]);
      return -1;
    }

    }

  element = jesy_get_key_value(ctx, root, "b");
  if (element) { printf("\n \"b\": %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]); }

  element = jesy_get_key_value(ctx, root, "c");
  if (element) { printf("\n \"c\": %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]); }

  element = jesy_get_key_value(ctx, root, "d");
  if (element) {
    int16_t idx = 0;
    struct jesy_element *el = NULL;
    do {
      el = jesy_get_array_value(ctx, element, idx);
      if (el) printf("\n    %d. %.*s <%s>", idx++, el->length, el->value, jesy_node_type_str[el->type]);
    } while(el);
  }
  if (element) {
    printf("\n \"d\": %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]);
    JESY_ARRAY_FOR_EACH(ctx, element) {
      printf("\n    %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]);
      if (element->type == JESY_ARRAY) {
        JESY_ARRAY_FOR_EACH(ctx, element) {
          printf("\n        %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]);
          if (element->type == JESY_ARRAY) {
            JESY_FOR_EACH(ctx, element, JESY_ARRAY) {
              printf("\n            %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]);
            }
          }
        }
      }
    }
  }

  element = jesy_get_key_value(ctx, root, "e");
  if (element) { printf("\n \"e\": %.*s <%s>", element->length, element->value, jesy_node_type_str[element->type]); }

  ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
  if (!ctx) {
    printf("\n Context initiation failed!");
    return -1;
  }
  struct jesy_element *it = NULL;
  it = jesy_add_object(ctx, it);
  it = jesy_add_key(ctx, it, "key");
  it = jesy_add_value(ctx, it, JESY_STRING, "value");
  it = jesy_get_root(ctx);
  it = jesy_add_key(ctx, it, "key2");
  it = jesy_add_array(ctx, it);
  jesy_add_value_true(ctx, it);
  jesy_add_value_false(ctx, it);
  jesy_add_value_null(ctx, it);
  jesy_add_value_number(ctx, it, "123.67");

  out_size = jesy_validate(ctx);
  printf("\n    Validation Error: %d - %s, size: %d", ctx->status, jesy_status_str[ctx->status], out_size);
  out_size = jesy_render(ctx, output, sizeof(output));
  printf("\n    Render Error: %d - %s, size: %d", ctx->status, jesy_status_str[ctx->status], out_size);
  printf("\n%.*s", out_size, output);

  jesy_update_key(ctx, jesy_get_root(ctx), "key", "new_key");
  it = jesy_get_key(ctx, jesy_get_root(ctx), "new_key");
  jesy_update_key_value(ctx, it, JESY_STRING, "new_value");

  it = jesy_get_key(ctx, jesy_get_root(ctx), "key2");
  jesy_update_key_value(ctx, it, JESY_NULL, "null");

  out_size = jesy_validate(ctx);
  printf("\n    Validation Error: %d - %s, size: %d", ctx->status, jesy_status_str[ctx->status], out_size);
  out_size = jesy_render(ctx, output, sizeof(output));
  printf("\n    Render Error: %d - %s, size: %d", ctx->status, jesy_status_str[ctx->status], out_size);
  printf("\n%.*s", out_size, output);
  return 0;
}