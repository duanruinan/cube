#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <assert.h>
#include <string.h>
#include <fcntl.h>
#include <cube_utils.h>
#include <cube_cache.h>

struct pending {
	struct list_head list;
	u8 ch;
};

void pending_free(struct pending *p, void *cache)
{
	if (!p)
		return;

	cb_cache_put(p, cache);
}

struct pending *pending_alloc(void *cache, bool clear)
{
	struct pending *p = cb_cache_get(cache, clear);

	return p;
}

s32 main(s32 argc, char **argv)
{
	void *cache;
	struct pending *p[9];
	s32 i;

	printf("sizeof list: %lu\n", sizeof(struct list_head));
	printf("sizeof pending: %lu\n", sizeof(struct pending));
	cache = cb_cache_create(sizeof(struct pending), 4);
	assert(cache);
	for (i = 0; i < 9; i++) {
		p[i] = cb_cache_get(cache, false);
		p[i]->ch = 'a' + i;
	}
	for (i = 0; i < 9; i++) {
		printf("p[%d]: %02X\n", i, p[i]->ch);
	}
	for (i = 0; i < 9; i++) {
		cb_cache_put(p[i], cache);
	}

	for (i = 0; i < 9; i++) {
		p[i] = cb_cache_get(cache, false);
		printf("p[%d]: %02X\n", i, p[i]->ch);
	}
	for (i = 0; i < 9; i++) {
		cb_cache_put(p[i], cache);
	}

	for (i = 0; i < 9; i++) {
		p[i] = cb_cache_get(cache, true);
		printf("p[%d]: %02X\n", i, p[i]->ch);
	}
	for (i = 0; i < 9; i++) {
		cb_cache_put(p[i], cache);
	}
	
	cb_cache_destroy(cache);
	return 0;
}

