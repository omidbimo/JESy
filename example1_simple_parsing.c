#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>

#include "jesy.h"
#include "jesy_util.h"

#define POOL_SIZE 0x800
static uint8_t mem_pool[POOL_SIZE];

char json_data[] = "{\"a\": \"Alpha\",\"b\": true,\"c\": 12345,\"d\": [true, [false, [-123456789, null], 3.9676, [\"Something else.\", false], null]], \"e\": {\"zero\": null, \"one\": 1, \"two\": 2, \"three\": [3], \"four\": [0, 1, 2, 3, 4]}, \"Empty\": null, \"h\": {\"a\": {\"b\": {\"c\": {\"d\": {\"e\": {\"f\": {\"Empty\": null}}}}}}},\"z\":[[[[[[[null,]]]]]]]}";

int main(void)
{
  struct jesy_context *ctx;
  jesy_status err;
  ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
  if (!ctx) {
    printf("\n Context initiation failed!");
    return -1;
  }

  if (0 != (err = jesy_parse(ctx, json_data, sizeof(json_data))))
  {
    printf("\n    Parsing Error: %d - %s", err, jesy_status_str[err]);
    return -1;
  }

  printf("\n Number of JSON elements: %d", ctx->node_count);
  printf("\n Pool usage%: %d", (sizeof(*ctx) + (ctx->node_count * sizeof(*ctx->root)))*100/sizeof(mem_pool));
}