
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "jes.h"

#if 0
void jes_print(struct jes_context *ctx)
{
  struct jes_node *node = ctx->pacx->root;
  if (!ctx->pacx->root) return;
  uint32_t idx;
  for (idx = 0; idx < ctx->pacx->allocated; idx++) {
    printf("\n    %d. %s,   parent:%d, right:%d, child:%d", idx, jes_node_type_str[ctx->pacx->pool[idx].data.type],
      ctx->pacx->pool[idx].parent, ctx->pacx->pool[idx].right, ctx->pacx->pool[idx].child);
  }
  return;
  while (node) {

    if (node->data.type == JES_NONE) {
      printf("\nEND! reached a JES_NONE");
      break;
    }

    if (node->data.type == JES_OBJECT) {
      printf("\n    { <%s> - @%d", jes_node_type_str[node->data.type]);
    } else if (node->data.type == JES_KEY) {
      printf("\n        %.*s <%s>: - @%d", node->data.length, node->data.value, jes_node_type_str[node->data.type]);
    } else if (node->data.type == JES_ARRAY) {
      //printf("\n            %.*s <%s>", node.size, &ctx->json_data[node.offset], jes_node_type_str[node.type]);
    } else {
      printf("\n            %.*s <%s> - @%d", node->data.length, node->data.value, jes_node_type_str[node->data.type]);
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
#endif

#if 0


void jes_print_tree(struct jes_context *ctx)
{
  struct jes_node *iter = ctx->pacx->root;

  int tabs = 0;
  int idx;
      printf("\n");
  while (iter) {
    printf("\n ---------->>> [%d] %s,   parent:[%d], right:%d, child:%d\n", iter - ctx->pacx->pool, jes_node_type_str[iter->data.type],
      iter->parent, iter->right, iter->child);
    switch (iter->data.type) {
      case JES_OBJECT:
        //for (idx = 0; idx < tabs; idx++)
        //{
        //  *dst++ = '\t';
        //}

        printf(" {\n");
        tabs++;
        break;
      case JES_KEY:
        for (idx = 0; idx < tabs; idx++) printf("\t");

        printf("\"%.*s\":", iter->data.length, iter->data.value);
        break;
      case JES_VALUE_STRING:
        printf(" \"%.*s\"", iter->data.length, iter->data.value);
        break;
      case JES_VALUE_NUMBER:
      case JES_VALUE_BOOLEAN:
      case JES_VALUE_NULL:
        printf(" %.*s", iter->data.length, iter->data.value);
        break;
      case JES_ARRAY:
        printf("[\n");
        break;
      default:
      case JES_NONE:
        printf("\n Serialize error! Node of unexpected type: %d", iter->data.type);
        return;
    }

    if (HAS_CHILD(iter)) {
      iter = jes_get_child_node(ctx->pacx, iter);
      continue;
    }

    if (iter->data.type == JES_OBJECT) {
      printf("\n");
      for (idx = 0; idx < tabs; idx++) printf("\t");
      printf("} !!!!\n");
      tabs--;
    }

    else if (iter->data.type == JES_ARRAY) {
      printf("\n");
      for (idx = 0; idx < tabs; idx++) printf("\t");
      printf("] ????\n");
    }

    if (HAS_RIGHT(iter)) {
      iter = jes_get_right_node(ctx->pacx, iter);
      printf(",\n");
      continue;
    }

     while (iter = jes_get_parent_node(ctx->pacx, iter)) {
      if (iter->data.type == JES_OBJECT) {
        printf("\n");
        for (idx = 0; idx < tabs; idx++) printf("\t");
        printf("} [%d]", iter - ctx->pacx->pool);
        tabs--;
      }
      else if (iter->data.type == JES_ARRAY) {
        printf("\n");
        for (idx = 0; idx < tabs; idx++) printf("\t");
        printf("]");
      }
      if (HAS_RIGHT(iter)) {
        printf("\nHAS_RIGHT [%d]", iter - ctx->pacx->pool);
        iter = jes_get_right_node(ctx->pacx, iter);
        printf(",\n");
        break;
      }
    }
  }
}
#endif
int main(void)
{
  struct jes_context *ctx;
  FILE *fp;
  char file_data[0x4FFFF];
  uint8_t mem_pool[0x4FFFF];
  char output[0x4FFFF];

  //printf("\nSize of jes_context: %d bytes", sizeof(struct jes_context));
  //printf("\nSize of jes_parser_context: %d bytes", sizeof(struct jes_parser_context));
  //printf("\nSize of jes_node: %d bytes", sizeof(struct jes_node));


  fp = fopen("test.json", "rb");

  if (fp != NULL) {
    fread(file_data, sizeof(char), sizeof(file_data), fp);
    if ( ferror( fp ) != 0 ) {
      fclose(fp);
      return 0;
    }
    fclose(fp);
  }
  //printf("\n\n\n %s", file_data);
  ctx = jes_init_context(mem_pool, sizeof(mem_pool));
  //printf("0x%lX, 0x%lX", mem_pool, ctx);
  if (!ctx) {
    printf("\n Context init failed!");
  }

  if (0 == jes_parse(ctx, file_data, sizeof(file_data))) {
    printf("\nSize of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
    //printf("\nMemory required: %d bytes for %d elements.", ctx->node_count*sizeof(struct jes_node), ctx->node_count);

    //jes_print(ctx);
    jes_serialize(ctx, output, sizeof(output));
    printf("\n\n%s", output);

    //jes_print_tree(ctx);
    printf("\nJSON length: %lld", strlen(output));
  }
  else {
    printf("\nFAILED");
  }
  return 0;
}