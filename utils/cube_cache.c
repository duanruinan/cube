/*
 * Copyright © 2020 Ruinan Duan, duanruinan@zoho.com 
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 */
#include <stdio.h>
#include <stdlib.h>
#include <malloc.h>
#include <assert.h>
#include <stdbool.h>
#include <errno.h>
#include <string.h>
#include <cube_utils.h>

struct cb_cache_e {
	struct list_head link;
	void *d;
};

struct cb_cache {
	size_t size;
	size_t real_size;
	s32 cache_nr;
	struct list_head list;
};

void cb_cache_destroy(void *c)
{
	struct cb_cache *cache = c;
	struct cb_cache_e *e, *n;

	if (!c)
		return;

	list_for_each_entry_safe(e, n, &cache->list, link) {
		list_del(&e->link);
		free(e->d);
		free(e);
	}

	free(cache);
}

void *cb_cache_create(size_t size, s32 cache_nr)
{
	struct cb_cache *cache;
	struct cb_cache_e *e;
	s32 i;

	if (!size || !cache_nr)
		return NULL;
	
	cache = calloc(1, sizeof(*cache));
	if (!cache)
		goto err;

	cache->size = size;
	cache->real_size = sizeof(void *) + size;
	cache->cache_nr = cache_nr;

	INIT_LIST_HEAD(&cache->list);

	for (i = 0; i < cache->cache_nr; i++) {
		e = calloc(1, sizeof(*e));
		if (!e)
			goto err;

		e->d = memalign(8, cache->real_size);
		if (!e->d) {
			free(e);
			goto err;
		}
		list_add_tail(&e->link, &cache->list);
	}

	return cache;

err:
	cb_cache_destroy(cache);
	return NULL;
}

static void cb_cache_append(struct cb_cache *cache)
{
	struct cb_cache_e *e;
	s32 i, j;

	if (!cache)
		return;

	for (i = 0; i < cache->cache_nr; i++) {
		e = calloc(1, sizeof(*e));
		if (!e)
			goto err;

		e->d = memalign(8, cache->real_size);
		if (!e->d) {
			free(e);
			goto err;
		}
		list_add_tail(&e->link, &cache->list);
	}

	return;

err:
	for (j = 0; j < i; j++) {
		e = list_last_entry(&cache->list, struct cb_cache_e, link);
		assert(e && e->link.next != &(e->link));
		list_del(&e->link);
	}
}

void *cb_cache_get(void *c, bool clear)
{
	struct cb_cache *cache = c;
	struct cb_cache_e *e, **f;
	void *r;

	if (!c)
		goto err;

	if (list_empty(&cache->list)) {
		cb_cache_append(cache);
	}

	e = list_first_entry(&cache->list, struct cb_cache_e, link);
	assert(e && e->link.next != &(e->link));

	list_del(&e->link);

	f = (struct cb_cache_e **)(e->d);
	*f = e;

	r = (u8 *)(e->d) + sizeof(struct cb_cache_e *);

	if (clear)
		memset((u8 *)r, 0, cache->size);

	return r;

err:
	return NULL;
}

void cb_cache_put(void *d, void *c)
{
	struct cb_cache *cache = c;
	struct cb_cache_e *e;

	if (!c || !d)
		return;

	e = *((struct cb_cache_e **)((u8 *)d - sizeof(struct cb_cache_e *)));
	assert(e);
	list_add_tail(&e->link, &cache->list);
}

