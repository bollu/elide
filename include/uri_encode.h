#include <stdlib.h>
#include <inttypes.h>

size_t uri_encode (const char *src, const size_t len, char *dst);
size_t uri_decode (const char *src, const size_t len, char *dst);
