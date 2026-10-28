/* the allocation helpers regex.c uses from AmiBash's util.c */
#include <stdlib.h>
#include <string.h>
void *xmalloc(size_t n) { return malloc(n ? n : 1); }
void *xrealloc(void *p, size_t n) { return realloc(p, n ? n : 1); }
void *xcalloc(size_t n) { return calloc(1, n ? n : 1); }
void xfree(void *p) { free(p); }
