
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include "Og_Chronometer.h"
#include "jesy.h"
  #define POOL_SIZE 0xFFFFFFF
static char file_data[0xFFFFFFF];
static uint8_t mem_pool[POOL_SIZE];
static char output[0xFFFFFFF];


struct test_param {
  struct jesy_context *ctx;
  void *file_data;
  size_t file_data_len;
  uint32_t err;
};

static int parse_loc(void *param)
{
  struct test_param *p = param;
  uint32_t Idx;
  for (Idx=0; Idx < 1 && p->err==0; Idx++) {
    p->ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
    p->err = jesy_parse(p->ctx, p->file_data, p->file_data_len);
  }
  //printf("\n %d", Idx);
  return 0;
}

static int serialize_loc(void *param)
{
  struct test_param *p = param;
  int32_t err;
  p->file_data_len = jesy_render(p->ctx, output, sizeof(output));
  err = p->ctx->status;
  return err;
}

static int init(void *param)
{
  struct test_param *p = param;
  p->ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
  return 0;
}

int main(void)
{

  struct jesy_context *ctx;
  FILE *fp;
  size_t out_size;
  jesy_status err;
  struct jesy_element *element;
  //printf("\nSize of jesy_context: %d bytes", sizeof(struct jesy_context));
  //printf("\nSize of jesy_parser_context: %d bytes", sizeof(struct jesy_parser_context));
  //printf("\nSize of jesy_node: %d bytes", sizeof(struct jesy_node));

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
  //printf("\n\n\n %s", file_data);
  ctx = jesy_init_context(mem_pool, sizeof(mem_pool));
  //printf("0x%lX, 0x%lX", mem_pool, ctx);
  if (!ctx) {
    printf("\n Context init failed!");
  }
    timeit_result bm;
    struct test_param param;
    param.ctx = ctx;
    param.file_data = file_data;
    param.file_data_len = sizeof(file_data);
    param.err = 0;
    bm = timeit(NULL, NULL, 0, NULL, NULL, NULL, NULL);
    printf("\n CPU tick: %d Hz, %f uS\n", bm.tick_freq, bm.tick_time_us);

    printf("\n JESy: Start parsing...");
    bm = timeit(parse_loc, &param, 1, init, &param, NULL, NULL);
    printf("\n Error: %d", param.err);
    printf("\n    min: %.3f ms, %ld ticks | avg: %.3f ms, %ld ticks | max: %.3f ms, %ld ticks\n",
            bm.min_time * 1000, bm.min_ticks, bm.avg_time * 1000, bm.avg_ticks, bm.max_time * 1000, bm.max_ticks);
    printf("\n      Size of JSON data: %lld bytes", strnlen(file_data, sizeof(file_data)));
    printf("\n      JESy node count: %d", ctx->node_count);

    printf("\n JESy: Start serializing...");
    bm = timeit(serialize_loc, &param, 1, NULL, NULL, NULL, NULL);
    printf("\n Error: %d", bm.err);
    printf("\n    min: %.3f ms, %ld ticks | avg: %.3f ms, %ld ticks | max: %.3f ms, %ld ticks\n",
            bm.min_time * 1000, bm.min_ticks, bm.avg_time * 1000, bm.avg_ticks, bm.max_time * 1000, bm.max_ticks);
    //out_size = strlen(output);
    //printf("\n\n%s", output);
    out_size = param.file_data_len;
    fp = fopen("result.json", "wb");
    printf("\n Size of JSON dump:  %d bytes",  out_size);
    if (fp != NULL) {
      fwrite(output, sizeof(char), out_size, fp);
      fclose(fp);
    }

  element = jesy_get(ctx, "a");
  if (element) {
    printf("\n \"a\": %.*s", element->length, element->value);
  }
  return 0;
}