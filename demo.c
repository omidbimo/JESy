
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "jesy.h"

#if 0
void jesy_print(struct jesy_context *ctx)
{
  struct jesy_node *node = ctx->pacx->root;
  if (!ctx->pacx->root) return;
  uint32_t idx;
  for (idx = 0; idx < ctx->pacx->allocated; idx++) {
    printf("\n    %d. %s,   parent:%d, right:%d, child:%d", idx, jesy_node_type_str[ctx->pacx->pool[idx].data.type],
      ctx->pacx->pool[idx].parent, ctx->pacx->pool[idx].right, ctx->pacx->pool[idx].child);
  }
  return;
  while (node) {

    if (node->data.type == JESY_NONE) {
      printf("\nEND! reached a JESY_NONE");
      break;
    }

    if (node->data.type == JESY_OBJECT) {
      printf("\n    { <%s> - @%d", jesy_node_type_str[node->data.type]);
    } else if (node->data.type == JESY_KEY) {
      printf("\n        %.*s <%s>: - @%d", node->data.length, node->data.value, jesy_node_type_str[node->data.type]);
    } else if (node->data.type == JESY_ARRAY) {
      //printf("\n            %.*s <%s>", node.size, &ctx->json_data[node.offset], jesy_node_type_str[node.type]);
    } else {
      printf("\n            %.*s <%s> - @%d", node->data.length, node->data.value, jesy_node_type_str[node->data.type]);
    }

    if (HAS_CHILD(node)) {
      node = jesy_get_child_node(ctx->pacx, node);
      continue;
    }

    if (HAS_RIGHT(node)) {
      node = jesy_get_right_node(ctx->pacx, node);
      continue;
    }

    while (node = jesy_get_parent_node(ctx->pacx, node)) {
      if (HAS_RIGHT(node)) {
        node = jesy_get_right_node(ctx->pacx, node);
        break;
      }
    }
  }
}
#endif

#if 0


void jesy_print_tree(struct jesy_context *ctx)
{
  struct jesy_node *iter = ctx->pacx->root;

  int tabs = 0;
  int idx;
      printf("\n");
  while (iter) {
    printf("\n ---------->>> [%d] %s,   parent:[%d], right:%d, child:%d\n", iter - ctx->pacx->pool, jesy_node_type_str[iter->data.type],
      iter->parent, iter->right, iter->child);
    switch (iter->data.type) {
      case JESY_OBJECT:
        //for (idx = 0; idx < tabs; idx++)
        //{
        //  *dst++ = '\t';
        //}

        printf(" {\n");
        tabs++;
        break;
      case JESY_KEY:
        for (idx = 0; idx < tabs; idx++) printf("\t");

        printf("\"%.*s\":", iter->data.length, iter->data.value);
        break;
      case JESY_VALUE_STRING:
        printf(" \"%.*s\"", iter->data.length, iter->data.value);
        break;
      case JESY_VALUE_NUMBER:
      case JESY_VALUE_BOOLEAN:
      case JESY_VALUE_NULL:
        printf(" %.*s", iter->data.length, iter->data.value);
        break;
      case JESY_ARRAY:
        printf("[\n");
        break;
      default:
      case JESY_NONE:
        printf("\n Serialize error! Node of unexpected type: %d", iter->data.type);
        return;
    }

    if (HAS_CHILD(iter)) {
      iter = jesy_get_child_node(ctx->pacx, iter);
      continue;
    }

    if (iter->data.type == JESY_OBJECT) {
      printf("\n");
      for (idx = 0; idx < tabs; idx++) printf("\t");
      printf("} !!!!\n");
      tabs--;
    }

    else if (iter->data.type == JESY_ARRAY) {
      printf("\n");
      for (idx = 0; idx < tabs; idx++) printf("\t");
      printf("] ????\n");
    }

    if (HAS_RIGHT(iter)) {
      iter = jesy_get_right_node(ctx->pacx, iter);
      printf(",\n");
      continue;
    }

     while (iter = jesy_get_parent_node(ctx->pacx, iter)) {
      if (iter->data.type == JESY_OBJECT) {
        printf("\n");
        for (idx = 0; idx < tabs; idx++) printf("\t");
        printf("} [%d]", iter - ctx->pacx->pool);
        tabs--;
      }
      else if (iter->data.type == JESY_ARRAY) {
        printf("\n");
        for (idx = 0; idx < tabs; idx++) printf("\t");
        printf("]");
      }
      if (HAS_RIGHT(iter)) {
        printf("\nHAS_RIGHT [%d]", iter - ctx->pacx->pool);
        iter = jesy_get_right_node(ctx->pacx, iter);
        printf(",\n");
        break;
      }
    }
  }
}
#endif
int main(void)
{
  #define POOL_SIZE 0x4FFFF
  struct jesy_context *ctx;
  FILE *fp;
  char file_data[0x4FFFF];
  uint8_t mem_pool[POOL_SIZE];
  char output[0x4FFFF];
  jesy_status err;
  //printf("\nSize of jesy_context: %d bytes", sizeof(struct jesy_context));
  //printf("\nSize of jesy_parser_context: %d bytes", sizeof(struct jesy_parser_context));
  //printf("\nSize of jesy_node: %d bytes", sizeof(struct jesy_node));


  fp = fopen("test.soc", "rb");

  if (fp != NULL) {
    fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }
  //printf("\n\n\n %s", file_data);
  ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
  //printf("0x%lX, 0x%lX", mem_pool, ctx);
  if (!ctx) {
    printf("\n Context init failed!");
  }
  printf("\n JESy: Start parsing...");
  if (0 == (err = jesy_parse(ctx, file_data, sizeof(file_data)))) {
    printf("\n JESy: Parsing end!");
    printf("\nSize of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
    //printf("\nMemory required: %d bytes for %d elements.", ctx->node_count*sizeof(struct jesy_node), ctx->node_count);

    //jesy_print(ctx);
    jesy_serialize(ctx, output, sizeof(output));
    //printf("\n\n%s", output);

    //jesy_reset_iterator(ctx);
    printf("\nHas \"StcRevData\": %s", jesy_find(ctx, "StcRevData") ? "True" : "False");
    printf("\nHas \"devData\": %s", jesy_find(ctx, "devData") ? "True" : "False");
    printf("\nHas \"MacId\": %s", jesy_find(ctx, "MacId") ? "True" : "False");
    printf("\nType: \"devData\": %d", jesy_get_type(ctx, "devData"));
    printf("\nType: \"IOs\": %d", jesy_get_type(ctx, "IOs"));

    //jesy_print_tree(ctx);
    printf("\nJSON length: %lld", strlen(output));
  }
  else {
    printf("\nFAILED, %d", err);
  }
  return 0;
}