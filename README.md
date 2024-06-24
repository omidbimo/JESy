# JESy

JSON for Embedded Systems is a lightweight [JSON ]([JSON](https://www.json.org/json-en.html))library implemented for Embedded targets with some restrictions such as no dynamic memory allocations or limited stack size to support recursion. The goal is to make JESy fully conform to the [JSON](complies with [RFC 8259](https://datatracker.ietf.org/doc/html/rfc8259) JSON standard,) standard.

JESy provides an API to parse JSON documents into a tree of JSON elements, manipulate the elements and then render the elements into a string.


## Key features

- No dynamic memory allocation. All objects will be stotred on a single working buffer.

- Non-recursive parser.

- Can process multiple JSON documents at the same time. The parser context is unique for each document and is mamintained on the working buffer.

-  It's fast since it doesn't copy any data from the source document. 

- No external dependencies

- Configurable to support/overwrite duplicate keys



## Usage

### Parse a JSON string

```c
#include "jesy.h"

const char input_data[] = "{ \"key\": \"value\" }";
static uint8_t buffer[0x1000];
jesy_status err;

struct jesy_context *doc = jesy_init_context(buffer, sizeof(buffer));
if (!doc) {
    /* Error handling... */
    return;
}

if (0 != (err = jesy_parse(doc, input_data , sizeof(input_data)))) {
    /* Error handling... */
    return;
}

TBC




```
