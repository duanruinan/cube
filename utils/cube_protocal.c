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
#include <string.h>
#include <assert.h>
#include <errno.h>
#include <cube_utils.h>
#include <cube_protocal.h>

u8 *cb_server_create_linkup_cmd(u64 link_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_LINK_ID_ACK_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_result = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_LINK_ID_ACK_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CB_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = link_id;
	*n = size;

	return p;
}

u8 *cb_dup_linkup_cmd(u8 *dst, u8 *src, u32 n, u64 link_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_link_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_link_id = (struct cb_tlv *)(dst
			+ map[CB_CMD_LINK_ID_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u64 *)(&tlv_link_id->payload[0])) = link_id;
	return dst;
}

u64 cb_client_parse_link_id(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_LINK_ID_ACK_SHIFT))) {
		fprintf(stderr, "not link id cmd\n");
		return 0;
	}

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_LINK_ID_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_LINK_ID_ACK_SHIFT - CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_client_create_surface_cmd(struct cb_surface_info *s, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_surface_create;
	u32 size, size_surface_create, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_surface_create = sizeof(*tlv) + sizeof(*s);
	size = sizeof(*tlv) + size_map + size_surface_create + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_CREATE_SURFACE_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_surface_create + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_surface_create = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_CREATE_SURFACE_SHIFT - CB_CMD_OFFSET]
		= (u8 *)tlv_surface_create - p;
	tlv_surface_create->tag = CB_TAG_CREATE_SURFACE;
	tlv_surface_create->length = sizeof(*s);
	if (s)
		memcpy(&tlv_surface_create->payload[0], s, sizeof(*s));
	*n = size;

	return p;
}

u8 *cb_dup_create_surface_cmd(u8 *dst, u8 *src, u32 n,
			       struct cb_surface_info *s)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_surface_create;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_surface_create = (struct cb_tlv *)(dst
			+ map[CB_CMD_CREATE_SURFACE_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_surface_create->payload[0], s, sizeof(*s));
	return dst;
}

s32 cb_server_parse_create_surface_cmd(u8 *data, struct cb_surface_info *s)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_surface_create;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_CREATE_SURFACE_SHIFT)))
		return -1;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_CREATE_SURFACE_SHIFT - CB_CMD_OFFSET] >= size)
		return -1;
	tlv_surface_create = (struct cb_tlv *)(data
			+ map[CB_CMD_CREATE_SURFACE_SHIFT-CB_CMD_OFFSET]);
	if (tlv_surface_create->tag != CB_TAG_CREATE_SURFACE)
		return -1;
	if (tlv_surface_create->length != sizeof(*s))
		return -1;
	memcpy(s, &tlv_surface_create->payload[0], sizeof(*s));
	return 0;
}

u8 *cb_server_create_surface_id_cmd(u64 surface_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_surface_id;
	u32 size, size_surface_id, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_surface_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_surface_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_CREATE_SURFACE_ACK_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_surface_id + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_surface_id = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_CREATE_SURFACE_ACK_SHIFT - CB_CMD_OFFSET]
		= (u8 *)tlv_surface_id - p;
	tlv_surface_id->tag = CB_TAG_RESULT;
	tlv_surface_id->length = sizeof(u64);
	*((u64 *)(&tlv_surface_id->payload[0])) = surface_id;
	*n = size;

	return p;
}

u8 *cb_dup_surface_id_cmd(u8 *dst, u8 *src, u32 n, u64 surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_surface_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_surface_id = (struct cb_tlv *)(dst
			+ map[CB_CMD_CREATE_SURFACE_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u32 *)(&tlv_surface_id->payload[0])) = surface_id;
	return dst;
}

u64 cb_client_parse_surface_id(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_CREATE_SURFACE_ACK_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_CREATE_SURFACE_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_CREATE_SURFACE_ACK_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_client_create_view_cmd(struct cb_view_info *v, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_view_create;
	u32 size, size_view_create, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_view_create = sizeof(*tlv) + sizeof(*v);
	size = sizeof(*tlv) + size_map + size_view_create + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_CREATE_VIEW_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_view_create + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_view_create = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_CREATE_VIEW_SHIFT - CB_CMD_OFFSET]
		= (u8 *)tlv_view_create - p;
	tlv_view_create->tag = CB_TAG_CREATE_VIEW;
	tlv_view_create->length = sizeof(*v);
	if (v)
		memcpy(&tlv_view_create->payload[0], v, sizeof(*v));
	*n = size;

	return p;
}

u8 *cb_dup_create_view_cmd(u8 *dst, u8 *src, u32 n, struct cb_view_info *v)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_view_create;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_view_create = (struct cb_tlv *)(dst
			+ map[CB_CMD_CREATE_VIEW_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_view_create->payload[0], v, sizeof(*v));
	return dst;
}

s32 cb_server_parse_create_view_cmd(u8 *data, struct cb_view_info *v)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_view_create;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_CREATE_VIEW_SHIFT)))
		return -1;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_CREATE_VIEW_SHIFT - CB_CMD_OFFSET] >= size)
		return -1;
	tlv_view_create = (struct cb_tlv *)(data
			+ map[CB_CMD_CREATE_VIEW_SHIFT-CB_CMD_OFFSET]);
	if (tlv_view_create->tag != CB_TAG_CREATE_VIEW)
		return -1;
	if (tlv_view_create->length != sizeof(*v))
		return -1;
	memcpy(v, &tlv_view_create->payload[0], sizeof(*v));
	return 0;
}

u8 *cb_server_create_view_id_cmd(u64 view_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_view_id;
	u32 size, size_view_id, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_view_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_view_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_CREATE_VIEW_ACK_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_view_id + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_view_id = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_CREATE_VIEW_ACK_SHIFT - CB_CMD_OFFSET]
		= (u8 *)tlv_view_id - p;
	tlv_view_id->tag = CB_TAG_RESULT;
	tlv_view_id->length = sizeof(u64);
	*((u64 *)(&tlv_view_id->payload[0])) = view_id;
	*n = size;

	return p;
}

u8 *cb_dup_view_id_cmd(u8 *dst, u8 *src, u32 n, u64 view_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_view_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_view_id = (struct cb_tlv *)(dst
			+ map[CB_CMD_CREATE_VIEW_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u32 *)(&tlv_view_id->payload[0])) = view_id;
	return dst;
}

u64 cb_client_parse_view_id(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_CREATE_VIEW_ACK_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_CREATE_VIEW_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_CREATE_VIEW_ACK_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_client_create_bo_cmd(struct cb_buffer_info *b, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_create;
	u32 size, size_bo_create, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_bo_create = sizeof(*tlv) + sizeof(*b);
	size = sizeof(*tlv) + size_map + size_bo_create + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CB_CMD_CREATE_BO_SHIFT;

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_bo_create + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_bo_create = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_CREATE_BO_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_bo_create - p;
	tlv_bo_create->tag = CB_TAG_CREATE_BO;
	tlv_bo_create->length = sizeof(*b);
	if (b)
		memcpy(&tlv_bo_create->payload[0], b, sizeof(*b));
	*n = size;

	return p;
}

u8 *cb_dup_create_bo_cmd(u8 *dst, u8 *src, u32 n, struct cb_buffer_info *b)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_create;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_bo_create = (struct cb_tlv *)(dst
			+ map[CB_CMD_CREATE_BO_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_bo_create->payload[0], b, sizeof(*b));
	return dst;
}

s32 cb_server_parse_create_bo_cmd(u8 *data, struct cb_buffer_info *b)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_create;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_CREATE_BO_SHIFT)))
		return -1;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_CREATE_BO_SHIFT - CB_CMD_OFFSET] >= size)
		return -1;
	tlv_bo_create = (struct cb_tlv *)(data
			+ map[CB_CMD_CREATE_BO_SHIFT-CB_CMD_OFFSET]);
	if (tlv_bo_create->tag != CB_TAG_CREATE_BO)
		return -1;
	if (tlv_bo_create->length != sizeof(*b))
		return -1;
	memcpy(b, &tlv_bo_create->payload[0], sizeof(*b));
	return 0;
}

u8 *cb_server_create_bo_id_cmd(u64 bo_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 size, size_bo_id, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_bo_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_bo_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CB_CMD_CREATE_BO_ACK_SHIFT;

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_bo_id + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_bo_id = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_CREATE_BO_ACK_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_bo_id - p;
	tlv_bo_id->tag = CB_TAG_RESULT;
	tlv_bo_id->length = sizeof(u64);
	*((u64 *)(&tlv_bo_id->payload[0])) = bo_id;
	*n = size;

	return p;
}

u8 *cb_dup_bo_id_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_bo_id = (struct cb_tlv *)(dst
			+ map[CB_CMD_CREATE_BO_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u32 *)(&tlv_bo_id->payload[0])) = bo_id;
	return dst;
}

u64 cb_client_parse_bo_id(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_CREATE_BO_ACK_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_CREATE_BO_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_CREATE_BO_ACK_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

/********************************************************/
u8 *cb_client_destroy_bo_cmd(u64 bo_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 size, size_bo_id, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_bo_id = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_bo_id + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_DESTROY_BO_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_bo_id + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_bo_id = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_DESTROY_BO_SHIFT - CB_CMD_OFFSET]
		= (u8 *)tlv_bo_id - p;
	tlv_bo_id->tag = CB_TAG_RESULT;
	tlv_bo_id->length = sizeof(u64);
	*((u64 *)(&tlv_bo_id->payload[0])) = bo_id;
	*n = size;

	return p;
}

u8 *cb_dup_destroy_bo_cmd(u8 *dst, u8 *src, u32 n, u64 bo_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_bo_id;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_bo_id = (struct cb_tlv *)(dst
			+ map[CB_CMD_DESTROY_BO_SHIFT-CB_CMD_OFFSET]);
	*((u32 *)(&tlv_bo_id->payload[0])) = bo_id;
	return dst;
}

u64 cb_server_parse_destroy_bo_cmd(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_DESTROY_BO_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_DESTROY_BO_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_DESTROY_BO_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

/********************************************************/

u8 *cb_client_create_commit_req_cmd(struct cb_commit_info *c, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_commit;
	u32 size, size_commit, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_commit = sizeof(*tlv) + sizeof(*c);
	size = sizeof(*tlv) + size_map + size_commit + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_COMMIT_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_commit + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_commit = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_COMMIT_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_commit - p;
	tlv_commit->tag = CB_TAG_COMMIT_INFO;
	tlv_commit->length = sizeof(*c);
	if (c)
		memcpy(&tlv_commit->payload[0], c, sizeof(*c));
	*n = size;

	return p;
}

u8 *cb_dup_commit_req_cmd(u8 *dst, u8 *src, u32 n, struct cb_commit_info *c)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_commit;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_commit = (struct cb_tlv *)(dst
			+ map[CB_CMD_COMMIT_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_commit->payload[0], c, sizeof(*c));
	return dst;
}

s32 cb_server_parse_commit_req_cmd(u8 *data, struct cb_commit_info *c)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_commit;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_COMMIT_SHIFT)))
		return -1;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_COMMIT_SHIFT - CB_CMD_OFFSET] >= size)
		return -1;
	tlv_commit = (struct cb_tlv *)(data
			+ map[CB_CMD_COMMIT_SHIFT-CB_CMD_OFFSET]);
	if (tlv_commit->tag != CB_TAG_COMMIT_INFO)
		return -1;
	if (tlv_commit->length != sizeof(*c))
		return -1;
	memcpy(c, &tlv_commit->payload[0], sizeof(*c));
	return 0;
}

u8 *cb_server_create_commit_ack_cmd(u64 ret, u64 surface_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_COMMIT_ACK_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_result = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_COMMIT_ACK_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CB_TAG_RESULT;
	tlv_result->length = sizeof(u64) + sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*(((u64 *)(&tlv_result->payload[0])) + 1) = surface_id;
	*n = size;

	return p;
}

u8 *cb_dup_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret, u64 surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct cb_tlv *)(dst
			+ map[CB_CMD_COMMIT_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*(((u64 *)(&tlv_result->payload[0])) + 1) = surface_id;
	return dst;
}

u64 cb_client_parse_commit_ack_cmd(u8 *data, u64 *surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_COMMIT_ACK_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_COMMIT_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_COMMIT_ACK_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != (sizeof(u64) + sizeof(u64)))
		return 0;
	*surface_id = *(((u64 *)(&tlv_result->payload[0])) + 1);
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_server_create_bo_complete_cmd(u64 ret, u64 surface_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_BO_COMPLETE_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_result = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_BO_COMPLETE_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CB_TAG_RESULT;
	tlv_result->length = sizeof(u64) + sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*(((u64 *)(&tlv_result->payload[0])) + 1) = surface_id;
	*n = size;

	return p;
}

u8 *cb_dup_bo_complete_cmd(u8 *dst, u8 *src, u32 n, u64 ret, u64 surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct cb_tlv *)(dst
			+ map[CB_CMD_BO_COMPLETE_SHIFT-CB_CMD_OFFSET]);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*(((u64 *)(&tlv_result->payload[0])) + 1) = surface_id;
	return dst;
}

u64 cb_client_parse_bo_complete_cmd(u8 *data, u64 *surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_BO_COMPLETE_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_BO_COMPLETE_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_BO_COMPLETE_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != (sizeof(u64) + sizeof(u64)))
		return 0;
	*surface_id = *(((u64 *)(&tlv_result->payload[0])) + 1);
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_server_create_bo_flipped_cmd(u64 ret, u64 surface_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_BO_FLIPPED_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_result = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_BO_FLIPPED_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CB_TAG_RESULT;
	tlv_result->length = sizeof(u64) + sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*(((u64 *)(&tlv_result->payload[0])) + 1) = surface_id;
	*n = size;

	return p;
}

u8 *cb_dup_bo_flipped_cmd(u8 *dst, u8 *src, u32 n, u64 ret, u64 surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct cb_tlv *)(dst
			+ map[CB_CMD_BO_FLIPPED_SHIFT-CB_CMD_OFFSET]);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*(((u64 *)(&tlv_result->payload[0])) + 1) = surface_id;
	return dst;
}

u64 cb_client_parse_bo_flipped_cmd(u8 *data, u64 *surface_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_BO_FLIPPED_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_BO_FLIPPED_SHIFT- CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_BO_FLIPPED_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != (sizeof(u64) + sizeof(u64)))
		return 0;
	*surface_id = *(((u64 *)(&tlv_result->payload[0])) + 1);
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_create_shell_cmd(struct cb_shell_info *s, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_shell;
	u32 size, size_shell, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_shell = sizeof(*tlv) + sizeof(*s);
	size = sizeof(*tlv) + size_map + size_shell + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CB_CMD_SHELL_SHIFT;

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_shell + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_shell = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_SHELL_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_shell - p;
	tlv_shell->tag = CB_TAG_SHELL;
	tlv_shell->length = sizeof(*s);
	if (s)
		memcpy(&tlv_shell->payload[0], s, sizeof(*s));
	*n = size;

	return p;
}

u8 *cb_dup_shell_cmd(u8 *dst, u8 *src, u32 n, struct cb_shell_info *s)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_shell;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_shell = (struct cb_tlv *)(dst
			+ map[CB_CMD_SHELL_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_shell->payload[0], s, sizeof(*s));
	return dst;
}

s32 cb_parse_shell_cmd(u8 *data, struct cb_shell_info *s)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_shell;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_SHELL_SHIFT)))
		return -1;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_SHELL_SHIFT - CB_CMD_OFFSET] >= size)
		return -1;
	tlv_shell = (struct cb_tlv *)(data
			+ map[CB_CMD_SHELL_SHIFT-CB_CMD_OFFSET]);
	if (tlv_shell->tag != CB_TAG_SHELL)
		return -1;
	if (tlv_shell->length != sizeof(*s))
		return -1;
	memcpy(s, &tlv_shell->payload[0], sizeof(*s));
	return 0;
}

u8 *cb_client_create_destroy_cmd(u64 link_id, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_destroy;
	u32 size, size_destroy, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_destroy = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_destroy + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_DESTROY_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_destroy + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_destroy = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_DESTROY_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_destroy - p;
	tlv_destroy->tag = CB_TAG_DESTROY;
	tlv_destroy->length = sizeof(u64);
	*((u64 *)(&tlv_destroy->payload[0])) = link_id;
	*n = size;

	return p;
}

u8 *cb_dup_destroy_cmd(u8 *dst, u8 *src, u32 n, u64 link_id)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_destroy;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_destroy = (struct cb_tlv *)(dst
			+ map[CB_CMD_DESTROY_SHIFT-CB_CMD_OFFSET]);
	*((u32 *)(&tlv_destroy->payload[0])) = link_id;
	return dst;
}

u64 cb_server_parse_destroy_cmd(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_destroy;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_DESTROY_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_DESTROY_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_destroy = (struct cb_tlv *)(data
			+ map[CB_CMD_DESTROY_SHIFT-CB_CMD_OFFSET]);
	if (tlv_destroy->tag != CB_TAG_DESTROY)
		return 0;
	if (tlv_destroy->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_destroy->payload[0]));
}

u8 *cb_server_create_destroy_ack_cmd(u64 ret, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_DESTROY_ACK_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_result = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_DESTROY_ACK_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CB_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*n = size;

	return p;
}

u8 *cb_dup_destroy_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct cb_tlv *)(dst
			+ map[CB_CMD_DESTROY_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u32 *)(&tlv_result->payload[0])) = ret;
	return dst;
}

u64 cb_client_parse_destroy_ack_cmd(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_DESTROY_ACK_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_DESTROY_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_DESTROY_ACK_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

struct cb_raw_input_event *cb_client_parse_raw_input_evt_cmd(u8 *data,
							     u32 *count_evts)
{
	struct cb_tlv *tlv;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_RAW_INPUT)
		return NULL;
	assert(!(tlv->length % sizeof(struct cb_raw_input_event)));
	*count_evts = tlv->length / sizeof(struct cb_raw_input_event);
	return (struct cb_raw_input_event *)(&tlv->payload[0]);
}

u8 *cb_client_create_get_kbd_led_st_cmd(u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE; /* magic or else */

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_GET_KBD_LED_STATUS;
	tlv->length = 0;
	*n = size;

	return p;
}

u8 *cb_server_create_get_kbd_led_st_ack_cmd(u32 led_status, u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32) + sizeof(led_status);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE; /* magic or else */

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_GET_KBD_LED_STATUS_ACK;
	tlv->length = sizeof(led_status);
	*((u32 *)(&tlv->payload[0])) = led_status;
	*n = size;

	return p;
}

u8 *cb_dup_get_kbd_led_st_ack_cmd(u8 *dst, u8 *src, u32 n, u32 led_status)
{
	struct cb_tlv *tlv;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	*((u32 *)(&tlv->payload[0])) = led_status;
	return dst;
}

s32 cb_client_parse_get_kbd_led_st_ack_cmd(u8 *data, u32 *led_status)
{
	struct cb_tlv *tlv;

	if (!led_status)
		return -EINVAL;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_GET_KBD_LED_STATUS_ACK)
		return -EINVAL;

	*led_status = *((u32 *)(&tlv->payload[0]));
	return 0;
}

u8 *cb_client_create_set_kbd_led_st_cmd(u32 led_status, u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32) + sizeof(led_status);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE; /* magic or else */

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_SET_KBD_LED;
	tlv->length = sizeof(led_status);
	*((u32 *)(&tlv->payload[0])) = led_status;
	*n = size;

	return p;
}

u8 *cb_dup_set_kbd_led_st_cmd(u8 *dst, u8 *src, u32 n, u32 led_status)
{
	struct cb_tlv *tlv;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	*((u32 *)(&tlv->payload[0])) = led_status;
	return dst;
}

s32 cb_server_parse_set_kbd_led_st_cmd(u8 *data, u32 *led_status)
{
	struct cb_tlv *tlv;

	if (!led_status || !data)
		return -EINVAL;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_SET_KBD_LED)
		return -EINVAL;

	*led_status = *((u32 *)(&tlv->payload[0]));

	return 0;
}

u8 *cb_client_create_raw_input_en_cmd(u64 en, u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32) + sizeof(u64);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE; /* magic or else */

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_RAW_INPUT_EN;
	tlv->length = sizeof(u64);
	memcpy(&tlv->payload[0], &en, tlv->length);
	*n = size;

	return p;
}

u8 *cb_dup_raw_input_en_cmd(u8 *dst, u8 *src, u32 n, u64 en)
{
	struct cb_tlv *tlv;
	u64 *p;

	if (!dst || !src)
		return NULL;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	p = (u64 *)(&tlv->payload[0]);
	*p = en;

	return dst;
}

s32 cb_server_parse_raw_input_en_cmd(u8 *data, u64 *en)
{
	struct cb_tlv *tlv;

	if (!en)
		return -EINVAL;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_RAW_INPUT_EN)
		return -EINVAL;

	*en = *((u64 *)(&tlv->payload[0]));
	return 0;
}

u8 *cb_server_create_hpd_cmd(struct cb_connector_info *conn_info, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_hpd;
	u32 size, size_hpd, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_hpd = sizeof(*tlv) + sizeof(*conn_info);
	size = sizeof(*tlv) + size_map + size_hpd + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 1 << CB_CMD_HPD_SHIFT;

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_hpd + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_hpd = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_HPD_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_hpd - p;
	tlv_hpd->tag = CB_TAG_RESULT;
	tlv_hpd->length = sizeof(u64);
	if (conn_info)
		memcpy(&tlv_hpd->payload[0], conn_info, sizeof(*conn_info));
	*n = size;

	return p;
}

u8 *cb_dup_hpd_cmd(u8 *dst, u8 *src, u32 n, struct cb_connector_info *conn_info)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_hpd;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_hpd = (struct cb_tlv *)(dst
			+ map[CB_CMD_HPD_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_hpd->payload[0], conn_info, sizeof(*conn_info));
	return dst;
}

u64 cb_client_parse_hpd_cmd(u8 *data, struct cb_connector_info *conn_info)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	if (!conn_info)
		return -EINVAL;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_HPD_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_HPD_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_HPD_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;

	memcpy(conn_info, &tlv_result->payload[0], sizeof(*conn_info));

	return 0;
}

void cb_cmd_dump(u8 *data)
{
	struct cb_tlv *tlv;
	u32 head;
	s32 i;

	printf("Dump command\n");
	head = *((u32 *)data);
	tlv = (struct cb_tlv *)(data + sizeof(u32));
	if (head & (1 << CB_CMD_LINK_ID_ACK_SHIFT)) {
		printf("LINKID_CMD\n");
	} else if (head & (1 << CB_CMD_CREATE_SURFACE_SHIFT)) {
		printf("CREATE_SURFACE_CMD\n");
	} else if (head & (1 << CB_CMD_CREATE_SURFACE_ACK_SHIFT)) {
		printf("CREATE_SURFACE_ACK_CMD\n");
	} else if (head & (1 << CB_CMD_CREATE_VIEW_SHIFT)) {
		printf("CREATE_VIEW_CMD\n");
	} else if (head & (1 << CB_CMD_CREATE_VIEW_ACK_SHIFT)) {
		printf("CREATE_VIEW_ACK_CMD\n");
	} else if (head & (1 << CB_CMD_CREATE_BO_SHIFT)) {
		printf("CREATE_BO_CMD\n");
	} else if (head & (1 << CB_CMD_CREATE_BO_ACK_SHIFT)) {
		printf("CREATE_BO_ACK_CMD\n");
	} else if (head & (1 << CB_CMD_DESTROY_BO_SHIFT)) {
		printf("DESTROY_BO_CMD\n");
	} else if (head & (1 << CB_CMD_COMMIT_SHIFT)) {
		printf("COMMIT_CMD\n");
	} else if (head & (1 << CB_CMD_COMMIT_ACK_SHIFT)) {
		printf("COMMIT_ACK_CMD\n");
	} else if (head & (1 << CB_CMD_BO_FLIPPED_SHIFT)) {
		printf("BO_FLIPPED_CMD\n");
	} else if (head & (1 << CB_CMD_BO_COMPLETE_SHIFT)) {
		printf("BO_COMPLETE_CMD\n");
	} else if (head & (1 << CB_CMD_DESTROY_SHIFT)) {
		printf("DESTROY_CMD\n");
	} else if (head & (1 << CB_CMD_DESTROY_ACK_SHIFT)) {
		printf("DESTROY_ACK_CMD\n");
	} else if (head & (1 << CB_CMD_SHELL_SHIFT)) {
		printf("SHELL_CMD\n");
	} else if (head & (1 << CB_CMD_HPD_SHIFT)) {
		printf("HPD_CMD\n");
	} else if (head & (1 << CB_CMD_MC_COMMIT_SHIFT)) {
		printf("MC_COMMIT_CMD\n");
	} else if (head & (1 << CB_CMD_MC_COMMIT_ACK_SHIFT)) {
		printf("MC_COMMIT_ACK_CMD\n");
	} else {
		printf("unknown command 0x%08X\n", head);
	}

	printf("length: %u\n", tlv->length);

	for (i = 0; i < tlv->length; i++) {
		printf("0x%02X ", data[i]);
		if (!((i + 1) % 8))
			printf("\n");
	}
}

u8 *cb_client_create_mc_commit_cmd(struct cb_mc_info *info, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_commit;
	u32 size, size_commit, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_commit = sizeof(*tlv) + sizeof(*info);
	size = sizeof(*tlv) + size_map + size_commit + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_MC_COMMIT_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_commit + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_commit = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_MC_COMMIT_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_commit - p;
	tlv_commit->tag = CB_TAG_MC_COMMIT_INFO;
	tlv_commit->length = sizeof(*info);
	if (info)
		memcpy(&tlv_commit->payload[0], info, sizeof(*info));
	*n = size;

	return p;
}

u8 *cb_dup_mc_commit_cmd(u8 *dst, u8 *src, u32 n, struct cb_mc_info *info)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_commit;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_commit = (struct cb_tlv *)(dst
			+ map[CB_CMD_MC_COMMIT_SHIFT-CB_CMD_OFFSET]);
	memcpy(&tlv_commit->payload[0], info, sizeof(*info));
	return dst;
}

s32 cb_server_parse_mc_commit_cmd(u8 *data, struct cb_mc_info *info)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_commit;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_MC_COMMIT_SHIFT)))
		return -1;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_MC_COMMIT_SHIFT - CB_CMD_OFFSET] >= size)
		return -1;
	tlv_commit = (struct cb_tlv *)(data
			+ map[CB_CMD_MC_COMMIT_SHIFT-CB_CMD_OFFSET]);
	if (tlv_commit->tag != CB_TAG_MC_COMMIT_INFO)
		return -1;
	if (tlv_commit->length != sizeof(*info))
		return -1;
	memcpy(info, &tlv_commit->payload[0], sizeof(*info));
	return 0;
}

u8 *cb_server_create_mc_commit_ack_cmd(u64 ret, u32 *n)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, size_result, size_map, *map, *head;
	u8 *p;

	size_map = CB_CMD_MAP_SIZE;
	size_result = sizeof(*tlv) + sizeof(u64);
	size = sizeof(*tlv) + size_map + size_result + sizeof(u32);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = (1 << CB_CMD_MC_COMMIT_ACK_SHIFT);

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_WIN;
	tlv->length = size_result + size_map;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	tlv_result = (struct cb_tlv *)(&tlv->payload[0] + size_map);
	tlv_map->tag = CB_TAG_MAP;
	tlv_map->length = CB_CMD_MAP_SIZE - sizeof(struct cb_tlv);
	map = (u32 *)(&tlv_map->payload[0]);
	map[CB_CMD_MC_COMMIT_ACK_SHIFT - CB_CMD_OFFSET] = (u8 *)tlv_result - p;
	tlv_result->tag = CB_TAG_RESULT;
	tlv_result->length = sizeof(u64);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	*n = size;

	return p;
}

u8 *cb_dup_mc_commit_ack_cmd(u8 *dst, u8 *src, u32 n, u64 ret)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 *map;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	tlv_result = (struct cb_tlv *)(dst
			+ map[CB_CMD_MC_COMMIT_ACK_SHIFT-CB_CMD_OFFSET]);
	*((u64 *)(&tlv_result->payload[0])) = ret;
	return dst;
}

u64 cb_client_parse_mc_commit_ack_cmd(u8 *data)
{
	struct cb_tlv *tlv, *tlv_map, *tlv_result;
	u32 size, *head, *map;

	head = (u32 *)data;
	if (!((*head) & (1 << CB_CMD_MC_COMMIT_ACK_SHIFT)))
		return 0;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	assert(tlv->tag == CB_TAG_WIN);
	size = sizeof(*tlv) + sizeof(u32) + tlv->length;
	tlv_map = (struct cb_tlv *)(&tlv->payload[0]);
	map = (u32 *)(&tlv_map->payload[0]);
	if (map[CB_CMD_MC_COMMIT_ACK_SHIFT - CB_CMD_OFFSET] >= size)
		return 0;
	tlv_result = (struct cb_tlv *)(data
			+ map[CB_CMD_MC_COMMIT_ACK_SHIFT-CB_CMD_OFFSET]);
	if (tlv_result->tag != CB_TAG_RESULT)
		return 0;
	if (tlv_result->length != sizeof(u64))
		return 0;
	return *((u64 *)(&tlv_result->payload[0]));
}

u8 *cb_client_create_set_cap_cmd(u64 cap, u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32) + sizeof(u64);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE; /* magic or else */

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_SET_CAPABILITY;
	tlv->length = sizeof(u64);
	memcpy(&tlv->payload[0], &cap, tlv->length);
	*n = size;

	return p;
}

u8 *cb_dup_set_cap_cmd(u8 *dst, u8 *src, u32 n, u64 cap)
{
	struct cb_tlv *tlv;
	u64 *p;

	if (!dst || !src)
		return NULL;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	p = (u64 *)(&tlv->payload[0]);
	*p = cap;

	return dst;
}

s32 cb_server_parse_set_cap_cmd(u8 *data, u64 *cap)
{
	struct cb_tlv *tlv;

	if (!cap)
		return -EINVAL;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_SET_CAPABILITY)
		return -EINVAL;

	*cap = *((u64 *)(&tlv->payload[0]));

	return 0;
}

u8 *cb_client_create_get_edid_cmd(u64 pipe, u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	u8 *p;

	size = sizeof(*tlv) + sizeof(u32) + sizeof(pipe);
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE; /* magic or else */

	tlv = (struct cb_tlv *)(p+sizeof(u32));
	tlv->tag = CB_TAG_GET_EDID;
	tlv->length = sizeof(pipe);
	memcpy(&tlv->payload[0], &pipe, tlv->length);
	*n = size;

	return p;
}

u8 *cb_dup_get_edid_cmd(u8 *dst, u8 *src, u32 n, u64 pipe)
{
	struct cb_tlv *tlv;
	u64 *p;

	if (!dst || !src || !n)
		return NULL;

	memcpy(dst, src, n);

	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	p = (u64 *)(&tlv->payload[0]);
	*p = pipe;

	return dst;
}

s32 cb_server_parse_get_edid_cmd(u8 *data, u64 *pipe)
{
	struct cb_tlv *tlv;

	if (!pipe)
		return -EINVAL;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_GET_EDID)
		return -EINVAL;

	*pipe = *((u64 *)(&tlv->payload[0]));

	return 0;
}

u8 *cb_server_create_get_edid_ack_cmd(u64 pipe, u8 *edid, u64 edid_sz,
				      bool avail, u32 *n)
{
	struct cb_tlv *tlv;
	u32 size, *head;
	struct edid_desc *edid_desc;
	u8 *p;

	if (!n)
		return NULL;

	size = sizeof(*tlv) + sizeof(u32) + sizeof(*edid_desc) + edid_sz;
	p = calloc(1, size);
	if (!p)
		return NULL;

	head = (u32 *)p;
	*head = 0xFE;

	tlv = (struct cb_tlv *)(p + sizeof(u32));
	tlv->tag = CB_TAG_GET_EDID_ACK;
	tlv->length = sizeof(*edid_desc) + edid_sz;
	edid_desc = (struct edid_desc *)(&(tlv->payload[0]));
	edid_desc->avail = avail;
	edid_desc->pipe = pipe;
	edid_desc->edid_sz = edid_sz;
	if (avail && edid)
		memcpy(&edid_desc->edid[0], edid, edid_sz);

	*n = size;

	return p;
}

u8 *cb_dup_get_edid_ack_cmd(u8 *dst, u8 *src, u32 n, u64 pipe, u8 *edid,
			    u64 edid_sz, bool avail)
{
	struct cb_tlv *tlv;
	struct edid_desc *edid_desc;

	if (!edid || !dst || !src)
		return NULL;

	memcpy(dst, src, n);
	tlv = (struct cb_tlv *)(dst+sizeof(u32));
	edid_desc = (struct edid_desc *)(&(tlv->payload[0]));
	edid_desc->avail = avail;
	edid_desc->pipe = pipe;
	edid_desc->edid_sz = edid_sz;
	if (avail)
		memcpy(&edid_desc->edid[0], edid, edid_sz);

	return dst;
}

s32 cb_client_parse_get_edid_ack_cmd(u8 *data, u64 *pipe, u8 *edid, u64 *sz)
{
	struct cb_tlv *tlv;
	struct edid_desc *edid_desc;

	if (!pipe || !edid || !sz)
		return -EINVAL;

	tlv = (struct cb_tlv *)(data+sizeof(u32));
	if (tlv->tag != CB_TAG_GET_EDID_ACK)
		return -EINVAL;

	edid_desc = (struct edid_desc *)(&tlv->payload[0]);
	*pipe = edid_desc->pipe;
	if (!edid_desc->avail) {
		*sz = 0;
		return -ENOENT; /* E-EDID not available */
	}

	*sz = edid_desc->edid_sz;
	memcpy(edid, &edid_desc->edid[0], edid_desc->edid_sz);

	return 0;
}

