
#include <stdio.h>
#include <stdint.h>
#include "jesy.h"
#include "jesy_util.h"

#define POOL_SIZE 4096
static uint8_t mem_pool[POOL_SIZE];
static char output[2048];


int main(void)
{
  const char json_data[] =
              "{\"menu\": {"
                  "\"header\": \"SVG Viewer\","
                  "\"file\": ["
                      "{\"id\": \"Open\"},"
                      "{\"id\": \"OpenNew\", \"label\": \"Open New\"},"
                      "{\"id\": \"SaveAs\", \"label\": \"Save As\"}"
                  "],"
                  "\"view\": ["
                      "{\"id\": \"ZoomIn\", \"label\": \"Zoom In\"},"
                      "{\"id\": \"ZoomOut\", \"label\": \"Zoom Out\"},"
                      "{\"id\": \"OriginalView\", \"label\": \"Original View\"}"
                  "],"
                  "\"playback\": ["
                      "{\"id\": \"Quality\"},"
                      "{\"id\": \"Pause\"},"
                      "{\"id\": \"Mute\"},"
                      "{\"id\": \"playback speed\"}"
                  "],"
                  "\"edit\": ["
                      "{\"id\": \"Find\", \"label\": \"Find...\"},"
                      "{\"id\": \"FindAgain\", \"label\": \"Find Again\"},"
                      "{\"id\": \"Copy\"},"
                      "{\"id\": \"CopyAgain\", \"label\": \"Copy Again\"},"
                      "{\"id\": \"CopySVG\", \"label\": \"Copy SVG\"},"
                      "{\"id\": \"ViewSVG\", \"label\": \"View SVG\"},"
                      "{\"id\": \"ViewSource\", \"label\": \"View Source\"}"
                  "],"
                  "\"?\": ["
                      "{\"id\": \"Help\"},"
                      "{\"id\": \"About\", \"label\": \"About Adobe CVG Viewer...\"}"
                  "]"
              "}}";

  size_t out_size, array_size;
  jesy_status err;
  int index;
  struct jesy_element *element, *key, *object, *items, *value;
  struct jesy_element *root;

  struct jesy_context *jdoc = jesy_init_context(mem_pool, sizeof(mem_pool));
  if (!jdoc) {
    /* Add your error handling here. */
    printf("\n Context initiation failed!");
    return -1;
  }

  printf("\n JESy - parsing the JSON string...");
  if (0 != (err = jesy_parse(jdoc, json_data, sizeof(json_data))))
  {
    /* Add your error handling here. */
    printf("\n    Parsing Error: %d - %s", err, jesy_status_str[err]);
    return -1;
  }

  printf("\n    Number of JSON elements: %d", jdoc->node_count);
  printf("\n    Pool usage%%: %ld", (sizeof(*jdoc) + (jdoc->node_count * sizeof(*jdoc->root)))*100/sizeof(mem_pool));

  /* Accessing a key value */
  element = jesy_get_key_value(jdoc, jesy_get_root(jdoc), "menu.header");
  if (element) {
    printf("\n    menu.header: \"%.*s\"", element->length, element->value);
  }
  else {
    /* Add your error handling. */
  }

  /* Iterating an array */
  items = jesy_get_key_value(jdoc, jesy_get_root(jdoc), "menu.file");
  if (items && items->type == JESY_ARRAY) {
    JESY_ARRAY_FOR_EACH(jdoc, items, object) {
      value = jesy_get_key_value(jdoc, object, "id");
      if (value) {
        printf("\n    menu.file.id: \"%.*s\"", value->length, value->value);
      }
    }
  }
  else {
    /* Add your error handling. */
  }

  /* Accessing array members */
  items = jesy_get_key_value(jdoc, jesy_get_root(jdoc), "menu.edit");
  array_size = jesy_get_array_size(jdoc, items);
  printf("\n menu.edit has %d elements.", array_size);
  for (index = 0; index < array_size; index++) {
    value = jesy_get_key_value(jdoc, jesy_get_array_value(jdoc, items, index), "id");
    if (value) printf("\n    menu.edit.id[%d]: \"%.*s\"", index, value->length, value->value);
  }
  /* Accessing array members in reverse order */
  index = 0;
  while (true) {
    value = jesy_get_key_value(jdoc, jesy_get_array_value(jdoc, items, --index), "id");
    if (!value) {
      break;
    }
    printf("\n    menu.edit.id[%d]: \"%.*s\"", index, value->length, value->value);
  }


  return 0;
}