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
#include <ctype.h>
#include <errno.h>
#include <stdint.h>
#include <sys/mman.h>
#include <stdbool.h>
#include <time.h>
#include <fcntl.h>
#include <unistd.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_protocal.h>
#include <cube_scanout.h>
#include <cube_client_agent.h>

static enum cb_log_level clia_dbg = CB_LOG_NOTICE;

#define clia_debug(fmt, ...) do { \
	if (clia_dbg >= CB_LOG_DEBUG) { \
		cb_tlog("[CLIA][DEBUG ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define clia_info(fmt, ...) do { \
	if (clia_dbg >= CB_LOG_INFO) { \
		cb_tlog("[CLIA][INFO  ] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define clia_notice(fmt, ...) do { \
	if (clia_dbg >= CB_LOG_NOTICE) { \
		cb_tlog("[CLIA][NOTICE] "fmt, ##__VA_ARGS__); \
	} \
} while (0);

#define clia_warn(fmt, ...) do { \
	cb_tlog("[CLIA][WARN  ] " fmt, ##__VA_ARGS__); \
} while (0);

#define clia_err(fmt, ...) do { \
	cb_tlog("[CLIA][ERROR ] " fmt, ##__VA_ARGS__); \
} while (0);

static void cb_client_agent_send_surface_ack(struct cb_client_agent *client,
					     void *surface)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_surface_id_cmd(client->surface_id_created_tx_cmd,
				  client->surface_id_created_tx_cmd_t,
				  client->surface_id_created_tx_len,
				  (u64)surface);
	if (!p) {
		clia_err("failed to dup surface ack");
		return;
	}

	length = client->surface_id_created_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send surface ack length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send surface ack length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock,client->surface_id_created_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send surface ack cmd: %llu", length);
	if (ret < 0) {
		clia_err("failed to send surface ack cmd %p. %s", surface,
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_view_ack(struct cb_client_agent *client,
					  void *view)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_view_id_cmd(client->view_id_created_tx_cmd,
			       client->view_id_created_tx_cmd_t,
			       client->view_id_created_tx_len,
			       (u64)view);
	if (!p) {
		clia_err("failed to dup view ack");
		return;
	}

	length = client->view_id_created_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send view ack length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send view ack length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->view_id_created_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send view ack cmd: %llu", length);
	if (ret < 0) {
		clia_err("failed to send view ack cmd %p. %s", view,
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_bo_create_ack(struct cb_client_agent *client,
					       void *bo)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_bo_id_cmd(client->bo_id_created_tx_cmd,
			     client->bo_id_created_tx_cmd_t,
			     client->bo_id_created_tx_len,
			     (u64)bo);
	if (!p) {
		clia_err("failed to dup bo commit ack");
		return;
	}

	length = client->bo_id_created_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo id created length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo id created length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->bo_id_created_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo id created: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo id created ack %p. %s", bo,
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_bo_commit_ack(struct cb_client_agent *client,
					       u64 result, u64 surface_id)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_commit_ack_cmd(client->bo_commit_ack_tx_cmd,
				  client->bo_commit_ack_tx_cmd_t,
				  client->bo_commit_ack_tx_len,
				  result, surface_id);
	if (!p) {
		clia_err("failed to dup bo commit ack");
		return;
	}

	length = client->bo_commit_ack_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo commit ack length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo commit ack length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->bo_commit_ack_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo commit ack: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo commit ack. %s", strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_bo_flipped(struct cb_client_agent *client,
					    void *bo, u64 surface_id)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_bo_flipped_cmd(client->bo_flipped_tx_cmd,
				  client->bo_flipped_tx_cmd_t,
				  client->bo_flipped_tx_len,
				  (u64)bo, surface_id);
	if (!p) {
		clia_err("failed to dup bo flipped");
		return;
	}

	length = client->bo_flipped_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo flipped length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo flipped length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->bo_flipped_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo flipped: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo flipped %p, %s", bo,
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_bo_complete(struct cb_client_agent *client,
					     void *bo, u64 surface_id)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_bo_complete_cmd(client->bo_complete_tx_cmd,
				   client->bo_complete_tx_cmd_t,
				   client->bo_complete_tx_len,
				   (u64)bo, surface_id);
	if (!p) {
		clia_err("failed to dup bo complete");
		return;
	}

	length = client->bo_complete_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo complete length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo complete length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->bo_complete_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send bo complete: %llu", length);
	if (ret < 0) {
		clia_err("failed to send bo complete %p, %s", bo,
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_raw_input(struct cb_client_agent *client,
					   u8 *evts,
					   u32 count_evts)
{
	size_t length;
	struct cb_tlv *tlv;
	s32 ret;
	u32 *head;

	tlv = (struct cb_tlv *)(evts + sizeof(u32));
	tlv->length = count_evts * sizeof(struct cb_raw_input_event);
	length = tlv->length + sizeof(*tlv) + sizeof(u32);

	head = (u32 *)evts;
	*head = 0xFE; /* magic or else */

	tlv->tag = CB_TAG_RAW_INPUT;

	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send raw input length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send raw input length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, evts, length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send raw input evts: %llu", length);
	if (ret < 0) {
		clia_err("failed to send raw input evts. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_hpd_evt(struct cb_client_agent *client,
					 struct cb_connector_info *conn_info)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_hpd_cmd(client->hpd_tx_cmd,
			   client->hpd_tx_cmd_t,
			   client->hpd_tx_len,
			   conn_info);
	if (!p) {
		clia_err("failed to dup hpd cmd");
		return;
	}

	length = client->hpd_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send hpd cmd length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send hpd cmd length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->hpd_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send hpd cmd: %llu", length);
	if (ret < 0) {
		clia_err("failed to send hpd cmd %s", strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_view_focus_cfg(struct cb_client_agent *client,
						void *v, bool on)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_view_focus_chg_cmd(client->view_focus_chg_cmd,
				      client->view_focus_chg_cmd_t,
				      client->view_focus_chg_len,
				      (u64)v, on);
	if (!p) {
		clia_err("failed to dup view focus chg command");
		return;
	}

	length = client->view_focus_chg_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send view focus chg command length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send view focus chg command length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->view_focus_chg_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send view focus chg command: %llu", length);
	if (ret < 0) {
		clia_err("failed to send view focus chg command. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_mc_commit_ack(struct cb_client_agent *client,
					       u64 result)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_mc_commit_ack_cmd(client->mc_commit_ack_tx_cmd,
				     client->mc_commit_ack_tx_cmd_t,
				     client->mc_commit_ack_tx_len,
				     result);
	if (!p) {
		clia_err("failed to dup mc commit ack");
		return;
	}

	length = client->mc_commit_ack_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send mc commit ack length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send mc commit ack length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->mc_commit_ack_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send mc commit ack: %llu", length);
	if (ret < 0) {
		clia_err("failed to send mc commit ack. %s", strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_get_and_send_edid(struct cb_client_agent *client,
					      u64 output_index)
{
	struct compositor *c;
	s32 ret;
	s32 pipe = output_index;
	u8 *p, *edid_tmp;
	size_t edid_tmp_sz, length;
	bool avail;

	if (!client)
		return;

	c = client->c;
	edid_tmp = (u8 *)malloc(512);
	assert(edid_tmp);
	ret = c->retrieve_edid(c, pipe, edid_tmp, &edid_tmp_sz);
	if (ret < 0) {
		clia_err("failed to get E-EDID for pipe %lu", pipe);
		if (ret == -ENOENT)
			clia_err("E-EDID not available for pipe %lu", pipe);
		avail = false;
	} else {
		avail = true;
		clia_notice("get E-EDID for pipe %lu ok.", pipe);
	}

	p = cb_dup_get_edid_ack_cmd(client->get_edid_ack_cmd,
				    client->get_edid_ack_cmd_t,
				    client->get_edid_ack_len,
				    output_index, edid_tmp, edid_tmp_sz, avail);
	if (!p) {
		clia_err("failed to dup EDID ack cmd.");
		return;
	}
	free(edid_tmp);

	length = client->get_edid_ack_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send get edid ack cmd length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send get edid ack cmd length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->get_edid_ack_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send get edid ack cmd: %llu", length);
	if (ret < 0) {
		clia_err("failed to send get edid ack cmd %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_kbd_led_status(struct cb_client_agent *client,
						u32 led_status)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_get_kbd_led_st_ack_cmd(client->kbd_led_status_ack_cmd,
					  client->kbd_led_status_ack_cmd_t,
					  client->kbd_led_status_ack_len,
					  led_status);
	if (!p) {
		clia_err("failed to dup get kbd led st ack cmd");
		return;
	}

	length = client->kbd_led_status_ack_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send get kbd led st ack cmd length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send get kbd led st ack cmd length. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->kbd_led_status_ack_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send kbd led status ack cmd: %llu", length);
	if (ret < 0) {
		clia_err("failed to send kbd led status ack cmd %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static void cb_client_agent_send_shell_cmd(struct cb_client_agent *client,
					   struct cb_shell_info *s)
{
	size_t length;
	s32 ret;
	u8 *p;

	p = cb_dup_shell_cmd(client->shell_tx_cmd,
			     client->shell_tx_cmd_t,
			     client->shell_tx_len,
			     s);
	if (!p) {
		clia_err("failed to dup shell cmd");
		return;
	}

	length = client->shell_tx_len;
	do {
		ret = cb_sendmsg(client->sock, (u8 *)&length, sizeof(size_t),
				 NULL);
	} while (ret == -EAGAIN);
	clia_debug("send shell cmd length: %llu", length);
	if (ret < 0) {
		clia_err("failed to send shell cmd. %s",
			 strerror(errno));
		client->c->rm_client(client->c, client);
		return;
	}

	do {
		ret = cb_sendmsg(client->sock, client->shell_tx_cmd,
				 length, NULL);
	} while (ret == -EAGAIN);
	clia_debug("send shell cmd: %llu", length);
	if (ret < 0) {
		clia_err("failed to send shell cmd %p, %s", s,
			 strerror(errno));
		client->c->rm_client(client->c, client);
	}
}

static struct cb_buffer *import_shm_buf(struct cb_buffer_info *buffer_info)
{
	struct shm_buffer *shm_buf;

	shm_buf = calloc(1, sizeof(*shm_buf));
	if (!shm_buf)
		return NULL;

	memcpy(&shm_buf->base.info, buffer_info, sizeof(*buffer_info));

	shm_buf->shm.fd = buffer_info->fd[0];

	if (cb_shm_import(&shm_buf->shm, buffer_info->sizes[0],
			  buffer_info->fd[0]) < 0) {
		clia_err("failed to init share memory buffer.");
		return NULL;
	}

	return &shm_buf->base;
}

static void release_shm_buf(struct cb_buffer *buffer)
{
	struct shm_buffer *shm_buf = container_of(buffer, struct shm_buffer,
						  base);

	if (!buffer) {
		clia_err("buffer is null");
		return;
	}

	clia_notice("destroy shm bo");
	cb_shm_release(&shm_buf->shm);
	free(shm_buf);
}

static void destroy_bo(struct cb_client_agent *client, struct cb_buffer *buffer)
{
	struct cb_surface *s;

	list_del(&buffer->link);
	list_for_each_entry(s, &client->surfaces, link) {
		if (s->buffer_pending == buffer) {
			s->buffer_pending = NULL;
		}
	}

	clia_warn("destroy bo: %lX", (u64)buffer);
	switch (buffer->info.type) {
	case CB_BUF_TYPE_SHM:
		clia_warn("release shm-buf");
		release_shm_buf(buffer);
		break;
	case CB_BUF_TYPE_DMA:
		clia_warn("release dma-buf");
		list_del(&buffer->dma_buf_flipped_l.link);
		list_del(&buffer->dma_buf_completed_l.link);
		if (!buffer->info.composed)
			client->c->release_so_dmabuf(client->c, buffer);
		else
			client->c->release_rd_dmabuf(client->c, buffer);
		break;
	default:
		break;
	}
}

static void surface_destroy(struct cb_client_agent *client,
			    struct cb_surface *s)
{
	struct cb_buffer *b, *next_b;

	clia_notice("remove a surface");
	list_del(&s->link);
	cb_signal_emit(&s->destroy_signal, NULL);
	if (s->view) {
		client->c->rm_view_from_comp(client->c, s->view);
		free(s->view);
	}
	if (s->use_renderer)
		cb_signal_rm(&s->flipped_l);
	list_for_each_entry_safe(b, next_b, &client->buffers, link) {
		clia_warn("clear buffer remained %p", b);
		destroy_bo(client, b);
	}
	cb_signal_fini(&s->destroy_signal);
	free(s);
}

void cb_client_agent_destroy(struct cb_client_agent *client)
{
	struct cb_surface *s, *n;
	if (!client)
		return;

	list_for_each_entry_safe(s, n, &client->surfaces, link) {
		surface_destroy(client, s);
	}

	if (client->sock_source) {
		cb_event_source_remove(client->sock_source);
		client->sock_source = NULL;
	}

	if (client->sock) {
		close(client->sock);
		client->sock = 0;
	}

	if (client->surface_id_created_tx_cmd_t)
		free(client->surface_id_created_tx_cmd_t);
	if (client->surface_id_created_tx_cmd)
		free(client->surface_id_created_tx_cmd);

	if (client->view_id_created_tx_cmd_t)
		free(client->view_id_created_tx_cmd_t);
	if (client->view_id_created_tx_cmd)
		free(client->view_id_created_tx_cmd);

	if (client->bo_id_created_tx_cmd_t)
		free(client->bo_id_created_tx_cmd_t);
	if (client->bo_id_created_tx_cmd)
		free(client->bo_id_created_tx_cmd);

	if (client->bo_commit_ack_tx_cmd_t)
		free(client->bo_commit_ack_tx_cmd_t);
	if (client->bo_commit_ack_tx_cmd)
		free(client->bo_commit_ack_tx_cmd);

	if (client->bo_flipped_tx_cmd_t)
		free(client->bo_flipped_tx_cmd_t);
	if (client->bo_flipped_tx_cmd)
		free(client->bo_flipped_tx_cmd);

	if (client->bo_complete_tx_cmd_t)
		free(client->bo_complete_tx_cmd_t);
	if (client->bo_complete_tx_cmd)
		free(client->bo_complete_tx_cmd);

	if (client->hpd_tx_cmd_t)
		free(client->hpd_tx_cmd_t);
	if (client->hpd_tx_cmd)
		free(client->hpd_tx_cmd);

	if (client->mc_commit_ack_tx_cmd_t)
		free(client->mc_commit_ack_tx_cmd_t);
	if (client->mc_commit_ack_tx_cmd)
		free(client->mc_commit_ack_tx_cmd);

	if (client->shell_tx_cmd_t)
		free(client->shell_tx_cmd_t);
	if (client->shell_tx_cmd)
		free(client->shell_tx_cmd);

	if (client->kbd_led_status_ack_cmd_t)
		free(client->kbd_led_status_ack_cmd_t);
	if (client->kbd_led_status_ack_cmd)
		free(client->kbd_led_status_ack_cmd);

	if (client->get_edid_ack_cmd_t)
		free(client->get_edid_ack_cmd_t);
	if (client->get_edid_ack_cmd)
		free(client->get_edid_ack_cmd);

	if (client->view_focus_chg_cmd_t)
		free(client->view_focus_chg_cmd_t);
	if (client->view_focus_chg_cmd)
		free(client->view_focus_chg_cmd);

	free(client);
}

static void destroy_cb(void *data)
{
	struct cb_client_agent *client = data;

	if (!data)
		return;

	cb_client_agent_destroy(client);
}

static void cb_client_agent_destroy_pending(struct cb_client_agent *client)
{
	if (client->destroy_idle_source)
		return;

	client->destroy_idle_source = cb_event_loop_add_idle(client->loop,
							     destroy_cb,
							     client);
}

static void bo_destroy_proc(struct cb_client_agent *client, u8 *buf)
{
	struct cb_buffer *buffer;

	buffer = (struct cb_buffer *)cb_server_parse_destroy_bo_cmd(buf);
	if (!buffer) {
		clia_err("failed to parse destroy bo command");
		return;
	}

	destroy_bo(client, buffer);
}

static s32 dma_buf_bo_commit_proc(struct cb_client_agent *client,
				  struct cb_commit_info *info)
{
	struct cb_buffer *buffer;
	struct cb_surface *s;
	struct cb_view *v;
	u64 bo_id;
	s32 ret;

	bo_id = info->bo_id;
	s = (struct cb_surface *)(info->surface_id);
	v = s->view;

	buffer = (struct cb_buffer *)bo_id;

	cb_region_init_rect(&s->damage,
			    info->bo_damage.pos.x,
			    info->bo_damage.pos.y,
			    info->bo_damage.w,
			    info->bo_damage.h);

	v->area.pos.x = info->view_x;
	v->area.pos.y = info->view_y;
	v->area.w = info->view_width;
	v->area.h = info->view_height;
	v->pipe_locked = info->pipe_locked;

	s->buffer_pending = buffer;
	clia_debug("commit dmabuf");
	s->use_renderer = false;
	ret = client->c->commit_dmabuf(client->c, s);

	cb_region_fini(&s->damage);
	return ret;
}

static void surface_bo_commit_proc(struct cb_client_agent *client,
				   struct cb_commit_info *info)
{
	struct cb_buffer *buffer;
	struct cb_surface *s;
	struct cb_view *v;
	u64 bo_id;

	bo_id = info->bo_id;
	s = (struct cb_surface *)(info->surface_id);
	v = s->view;

	buffer = (struct cb_buffer *)bo_id;

	cb_region_init_rect(&s->damage,
			    info->bo_damage.pos.x,
			    info->bo_damage.pos.y,
			    info->bo_damage.w,
			    info->bo_damage.h);

	v->area.pos.x = info->view_x;
	v->area.pos.y = info->view_y;
	v->area.w = info->view_width;
	v->area.h = info->view_height;
	v->pipe_locked = info->pipe_locked;

	s->buffer_pending = buffer;
	clia_debug("commit surface");
	s->use_renderer = true;
	client->c->commit_surface(client->c, s);
	cb_region_fini(&s->damage);
}

static void bo_commit_proc(struct cb_client_agent *client, u8 *buf)
{
	struct cb_commit_info info;
	struct cb_buffer *buffer, *buffer_last;
	struct cb_surface *s;
	struct cb_view *v;
	u64 bo_id;

	if (cb_server_parse_commit_req_cmd(buf, &info) < 0) {
		clia_err("failed to parse bo commit.");
		cb_client_agent_send_bo_commit_ack(client, COMMIT_FAILED,
						   info.surface_id);
		return;
	}

	bo_id = info.bo_id;
	buffer = (struct cb_buffer *)(bo_id);
	if (!buffer) {
		clia_err("invalid buffer");
		cb_client_agent_send_bo_commit_ack(client, COMMIT_FAILED,
						   info.surface_id);
		return;
	}

	s = (struct cb_surface *)(info.surface_id);
	if (!s) {
		clia_err("invalid surface");
		cb_client_agent_send_bo_commit_ack(client, COMMIT_FAILED,
						   info.surface_id);
		return;
	}

	v = s->view;
	if (!v) {
		clia_err("invalid view");
		cb_client_agent_send_bo_commit_ack(client, COMMIT_FAILED,
						   info.surface_id);
		return;
	}

	if (buffer->info.type == CB_BUF_TYPE_DMA && !buffer->info.composed) {
		clia_debug("? buffer last = %lX", (u64)(s->buffer_last));
		if (s->buffer_last) {
			clia_warn("Replace last buffer %lX",
				  (u64)(s->buffer_last));
			printf("Replace last buffer %lX\n",
				  (u64)(s->buffer_last));
			buffer_last = s->buffer_last;
			if (dma_buf_bo_commit_proc(client, &info) < 0) {
				clia_err("failed to commit buffer 1");
				printf("failed to commit buffer 1\n");
				cb_client_agent_send_bo_commit_ack(client,
							COMMIT_FAILED,
							info.surface_id);
				if (buffer) {
					client->send_bo_complete(client,
								 buffer,
							info.surface_id);
				}
			} else {
				clia_debug("send complete %lX", buffer_last);
				printf("send complete %lX\n", (u64)buffer_last);
				client->send_bo_complete(client, buffer_last,
							 info.surface_id);
				cb_client_agent_send_bo_commit_ack(client,
							bo_id,
							info.surface_id);
				cb_client_agent_send_bo_commit_ack(client,
							COMMIT_REPLACE,
							info.surface_id);
			}
		} else {
			if (dma_buf_bo_commit_proc(client, &info) < 0) {
				clia_err("failed to commit buffer.");
				printf("failed to commit buffer.\n");
				cb_client_agent_send_bo_commit_ack(client,
							COMMIT_FAILED,
							info.surface_id);
				if (buffer)
					client->send_bo_complete(client,
								 buffer,
							info.surface_id);
			} else {
				cb_client_agent_send_bo_commit_ack(client,
							bo_id,
							info.surface_id);
			}
		}
	} else if (buffer->info.type == CB_BUF_TYPE_SHM ||
		   (buffer->info.type == CB_BUF_TYPE_DMA &&
		    buffer->info.composed)) {
		surface_bo_commit_proc(client, &info);
		cb_client_agent_send_bo_commit_ack(client, bo_id,
						   info.surface_id);
	} else {
		clia_err("unknown buffer type. %d", buffer->info.type);
		cb_client_agent_send_bo_commit_ack(client, COMMIT_FAILED,
						   info.surface_id);
	}
}

static void surface_create_proc(struct cb_client_agent *client, u8 *buf)
{
	struct cb_surface_info sinfo;
	struct cb_surface *s;
	s32 ret;

	ret = cb_server_parse_create_surface_cmd(buf, &sinfo);
	if (ret < 0) {
		clia_err("failed to parse surface create cmd.");
		cb_cmd_dump(buf);
		goto err;
	}

	/* create surface */
	s = calloc(1, sizeof(*s));
	if (!s)
		goto err;

	memset(s, 0, sizeof(*s));
	s->client_agent = client;
	s->is_opaque = sinfo.is_opaque;
	cb_region_init_rect(&s->damage, sinfo.damage.pos.x, sinfo.damage.pos.y,
			    sinfo.damage.w, sinfo.damage.h);
	cb_region_init_rect(&s->opaque, sinfo.opaque.pos.x, sinfo.opaque.pos.y,
			    sinfo.opaque.w, sinfo.opaque.h);
	s->width = sinfo.width;
	s->height = sinfo.height;
	cb_signal_init(&s->destroy_signal);

	list_add_tail(&s->link, &client->surfaces);
	INIT_LIST_HEAD(&s->flipped_l.link);
	
	cb_client_agent_send_surface_ack(client, s);

	return;
err:
	cb_client_agent_send_surface_ack(client, NULL);
}

static void view_create_proc(struct cb_client_agent *client, u8 *buf)
{
	struct cb_view_info vinfo;
	struct cb_view *v;
	struct cb_surface *s;
	s32 ret;

	ret = cb_server_parse_create_view_cmd(buf, &vinfo);
	if (ret < 0) {
		clia_err("failed to parse view create cmd.");
		cb_cmd_dump(buf);
		goto err;
	}

	if (!vinfo.surface_id) {
		clia_err("failed to parse view create cmd, illegal surface id");
		cb_cmd_dump(buf);
		goto err;
	}

	s = (struct cb_surface *)vinfo.surface_id;

	/* create view */
	v = calloc(1, sizeof(*v));
	if (!v)
		goto err;

	memset(v, 0, sizeof(*v));
	v->alpha = vinfo.alpha;
	v->surface = s;
	s->view = v;
	v->zpos = vinfo.zpos;
	v->float_view = vinfo.float_view;

	memcpy(&v->area, &vinfo.area, sizeof(struct cb_rect));

	client->c->add_view_to_comp(client->c, v);

	cb_client_agent_send_view_ack(client, v);

	return;
err:
	cb_client_agent_send_view_ack(client, NULL);
}

static void bo_create_proc(struct cb_client_agent *client, u8 *buf)
{
	struct cb_buffer_info buffer_info;
	struct cb_buffer *buffer;
	s32 ret, i;

	ret = cb_server_parse_create_bo_cmd(buf, &buffer_info);
	if (ret < 0) {
		clia_err("failed to parse bo create cmd.");
		cb_cmd_dump(buf);
		goto err;
	}

	switch (buffer_info.type) {
	case CB_BUF_TYPE_SHM:
		/* copy fds */
		for (i = 0; i < 4; i++) {
			if (i < client->ipc_fds.count) {
				buffer_info.fd[i] = client->ipc_fds.fds[i];
				client->ipc_fds.fds[i] = 0;
			} else {
				buffer_info.fd[i] = 0;
			}
		}
		client->ipc_fds.count = 0;

		printf("received shm bo fd: %d, import.\n", buffer_info.fd[0]);
		buffer = import_shm_buf(&buffer_info);
		if (!buffer) {
			printf("failed to import shm bo.\n");
			goto err;
		}
		printf("import shm bo ok.\n");
		break;
	case CB_BUF_TYPE_DMA:
		/* copy fds */
		for (i = 0; i < 4; i++) {
			if (i < client->ipc_fds.count) {
				buffer_info.fd[i] = client->ipc_fds.fds[i];
				client->ipc_fds.fds[i] = 0;
			} else {
				buffer_info.fd[i] = 0;
			}
		}
		client->ipc_fds.count = 0;

		if (!buffer_info.composed) {
			buffer = client->c->import_so_dmabuf(client->c,
							     &buffer_info);
		} else {
			buffer = client->c->import_rd_dmabuf(client->c,
							     &buffer_info);
		}
		if (!buffer)
			goto err;
		break;
	default:
		goto err;
	}

	list_add_tail(&buffer->link, &client->buffers);
	cb_client_agent_send_bo_create_ack(client, buffer);

	return;
err:
	cb_client_agent_send_bo_create_ack(client, NULL);
}

static void mc_proc(struct cb_client_agent *client, u8 *buf)
{
	struct cb_mc_info info;
	struct shm_buffer *buffer;
	u64 bo_id;
	s32 ret;

	if (cb_server_parse_mc_commit_cmd(buf, &info) < 0) {
		clia_err("failed to parse mc commit.");
		return;
	}

	switch (info.type) {
	case MC_CMD_TYPE_SET_CURSOR:
		bo_id = info.bo_id;
		buffer = container_of((struct cb_buffer *)bo_id,
				      struct shm_buffer, base);
		if (!buffer) {
			cb_client_agent_send_mc_commit_ack(client,
							   (u64)(-EINVAL));
			return;
		}
		ret = client->c->set_mouse_cursor(client->c, buffer->shm.map,
					info.cursor.w,
					info.cursor.h,
					info.cursor.w,
					info.cursor.hot_x,
					info.cursor.hot_y,
					info.alpha_src_pre_mul);
		if (ret < 0) {
			clia_err("failed to set cursor %d", ret);
			cb_client_agent_send_mc_commit_ack(client,
							   (u64)ret);
			return;
		}
		cb_client_agent_send_mc_commit_ack(client, 0);
		break;
	case MC_CMD_TYPE_SHOW:
		client->c->show_mouse_cursor(client->c);
		cb_client_agent_send_mc_commit_ack(client, 0);
		break;
	case MC_CMD_TYPE_HIDE:
		client->c->hide_mouse_cursor(client->c);
		cb_client_agent_send_mc_commit_ack(client, 0);
		break;
	default:
		return;
	}
}

static void shell_proc(struct cb_client_agent *client, u8 *buf)
{
	s32 ret;
	struct cb_shell_info shell_info;
	struct cb_rect *rc;
	struct cb_mode *mode;
	char mode_name[32];
	s32 i;

	ret = cb_parse_shell_cmd(buf, &shell_info);
	if (ret < 0) {
		clia_err("failed to parse shell cmd.");
		cb_cmd_dump(buf);
		return;
	}

	switch (shell_info.cmd) {
	case CB_SHELL_DEBUG_SETTING:
		clia_debug("setting debug level.");

		clia_debug("CLIA: %02X", shell_info.value.dbg_flags.clia_flag);
		clia_dbg = shell_info.value.dbg_flags.clia_flag;

		clia_debug("COMP: %02X", shell_info.value.dbg_flags.comp_flag);
		client->c->set_dbg_level(client->c,
					 shell_info.value.dbg_flags.comp_flag);

		clia_debug("SC: %02X", shell_info.value.dbg_flags.sc_flag);
		client->c->set_sc_dbg_level(client->c,
					shell_info.value.dbg_flags.sc_flag);

		clia_debug("RD: %02X", shell_info.value.dbg_flags.rd_flag);
		client->c->set_rd_dbg_level(client->c,
					shell_info.value.dbg_flags.rd_flag);
		break;
	case CB_SHELL_CANVAS_LAYOUT_SETTING:
		clia_debug("setting layout. nr: %d",
			   shell_info.value.layout.count_heads);
		for (i = 0; i < shell_info.value.layout.count_heads; i++) {
			rc = &shell_info.value.layout.cfg[i].desktop_rc;
			clia_debug("\tdesktop[%d]: %d,%d %ux%u mode: %p", i,
				   rc->pos.x, rc->pos.y, rc->w, rc->h,
				   shell_info.value.layout.cfg[i].mode_handle);
		}
		client->c->set_desktop_layout(client->c,
					      &shell_info.value.layout);
		break;
	case CB_SHELL_CANVAS_LAYOUT_QUERY:
		clia_debug("query layout.");
		memset(&shell_info.value.layout, 0,
		       sizeof(struct cb_canvas_layout));
		client->c->get_desktop_layout(client->c,
					      &shell_info.value.layout);
		for (i = 0; i < shell_info.value.layout.count_heads; i++) {
			rc = &shell_info.value.layout.cfg[i].desktop_rc;
			clia_debug("\tdesktop[%d]: %d,%d %ux%u mode: %p", i,
				   rc->pos.x, rc->pos.y, rc->w, rc->h,
				   shell_info.value.layout.cfg[i].mode_handle);
		}
		cb_client_agent_send_shell_cmd(client, &shell_info);
		break;
	case CB_SHELL_OUTPUT_VIDEO_TIMING_ENUMERATE:
		clia_debug("enumerate mode, filter_en: %c, filter: %d, "
			   "last mode: %p",
			   shell_info.value.ote.filter_en ? 'Y' : 'N',
			   shell_info.value.ote.filter_en ?
			   	shell_info.value.ote.enum_filter.mode : 0,
			   shell_info.value.ote.handle_last);
		mode = client->c->enumerate_timing(client->c,
					shell_info.value.ote.pipe, 
					shell_info.value.ote.handle_last,
					shell_info.value.ote.filter_en ?
					  &shell_info.value.ote.enum_filter
					  : NULL);
		shell_info.value.ote.handle_cur = mode;
		if (mode) {
			shell_info.value.mode.width = mode->width;
			shell_info.value.mode.height = mode->height;
			shell_info.value.mode.clock = mode->pixel_freq;
			shell_info.value.mode.vrefresh = mode->vrefresh;
			shell_info.value.mode.preferred = mode->preferred;
			clia_debug("Mode: %ux%u@%u clock: %u preferred: %c",
				   shell_info.value.mode.width,
				   shell_info.value.mode.height,
				   shell_info.value.mode.vrefresh,
				   shell_info.value.mode.clock,
				   shell_info.value.mode.preferred ? 'Y' : 'N');
		} else {
			clia_debug("no mode left.");
			memset(&shell_info.value.mode, 0,
			       sizeof(struct mode_info));
		}
		cb_client_agent_send_shell_cmd(client, &shell_info);
		break;
	case CB_SHELL_OUTPUT_VIDEO_TIMING_CREAT:
		clia_debug("create mode");
		clia_debug("\tclock: %d", shell_info.value.mode.clock);
		clia_debug("\twidth: %u", shell_info.value.mode.width);
		clia_debug("\ths_start: %u", shell_info.value.mode.hsync_start);
		clia_debug("\ths_end: %u", shell_info.value.mode.hsync_end);
		clia_debug("\thtotal: %u", shell_info.value.mode.htotal);
		clia_debug("\thskew: %u", shell_info.value.mode.hskew);
		clia_debug("\theight: %u", shell_info.value.mode.height);
		clia_debug("\tvs_start: %u", shell_info.value.mode.vsync_start);
		clia_debug("\tvs_end: %u", shell_info.value.mode.vsync_end);
		clia_debug("\tvtotal: %u", shell_info.value.mode.vtotal);
		clia_debug("\tvscan: %u", shell_info.value.mode.vscan);
		clia_debug("\tvrefresh: %u", shell_info.value.mode.vrefresh);
		clia_debug("\tI: %c", shell_info.value.mode.interlaced
				? 'Y':'N');
		clia_debug("\ths_pol: %c", shell_info.value.mode.pos_hsync
				? '+':'-');
		clia_debug("\tvs_pol: %c", shell_info.value.mode.pos_vsync
				? '+':'-');
		memset(mode_name, 0, 32);
		snprintf(mode_name, 32 - 1, "%ux%u%c@%u-cust",
			 shell_info.value.mode.width,
			 shell_info.value.mode.height,
			 shell_info.value.mode.interlaced ? 'i':'p',
			 shell_info.value.mode.vrefresh);
		mode = client->c->create_custom_timing(client->c,
				shell_info.value.modeset_pipe,
				shell_info.value.mode.clock,
				shell_info.value.mode.width,
				shell_info.value.mode.hsync_start,
				shell_info.value.mode.hsync_end,
				shell_info.value.mode.htotal,
				shell_info.value.mode.hskew,
				shell_info.value.mode.height,
				shell_info.value.mode.vsync_start,
				shell_info.value.mode.vsync_end,
				shell_info.value.mode.vtotal,
				shell_info.value.mode.vscan,
				shell_info.value.mode.vrefresh,
				shell_info.value.mode.interlaced,
				shell_info.value.mode.pos_hsync,
				shell_info.value.mode.pos_vsync,
				mode_name);
		if (!mode) {
			clia_err("failed to create custom mode.");
			shell_info.value.new_mode_handle = NULL;
		} else {
			shell_info.value.new_mode_handle = mode;
		}
		cb_client_agent_send_shell_cmd(client, &shell_info);
		break;
	default:
		break;
	}
}

static void ipc_proc(struct cb_client_agent *client)
{
	u8 *buf;
	size_t ipc_sz;
	u32 flag;
	struct cb_tlv *tlv;
	u64 cap, raw_input_en, pipe;
	u32 led_status;

	if (!client)
		return;

	ipc_sz = client->ipc_sz;
	buf = &client->ipc_buf[0] + sizeof(size_t);
	flag = *((u32 *)buf);
	tlv = (struct cb_tlv *)(buf + sizeof(u32));
	if (tlv->tag == CB_TAG_UNKNOWN) {
		clia_err("unknown TAG ~~~~~~~~~~~");
		return;
	}
	assert(ipc_sz == (tlv->length + sizeof(*tlv) + sizeof(flag)));

	switch (tlv->tag) {
	case CB_TAG_RAW_INPUT_EN:
		if (client->capability) {
			cb_server_parse_raw_input_en_cmd(buf, &raw_input_en);
			client->raw_input_en = raw_input_en;
			clia_notice("set raw input switch as %d", raw_input_en);
		}
		return;
	case CB_TAG_SET_KBD_LED:
		clia_debug("receive set keyboard led command.");
		if (cb_server_parse_set_kbd_led_st_cmd(buf, &led_status) < 0) {
			clia_err("failed to parse set keyboard led command.");
			return;
		}
		client->c->set_kbd_led_status(client->c, led_status);
		return;
	case CB_TAG_GET_KBD_LED_STATUS:
		clia_debug("receive get keyboard led command.");
		if (client->c->get_kbd_led_status(client->c, &led_status) < 0) {
			cb_client_agent_send_kbd_led_status(client, 0);
		} else {
			cb_client_agent_send_kbd_led_status(client, led_status);
		}
		return;
	case CB_TAG_SET_CAPABILITY:
		if (cb_server_parse_set_cap_cmd(buf, &cap) < 0) {
			clia_err("failed to parse client capability.");
		} else {
			clia_notice("receive set capability command. %016lX",
				    cap);
			client->capability = cap;
		}
		return;
	case CB_TAG_GET_EDID:
		if (cb_server_parse_get_edid_cmd(buf, &pipe) < 0) {
			clia_err("failed to parse get edid cmd.");
		} else {
			clia_notice("receive get edid cmd. pipe: %lu", pipe);
			cb_client_agent_get_and_send_edid(client, pipe);
		}
		return;
	case CB_TAG_WIN:
		break;
	default:
		clia_err("illegal tag: %08X", tlv->tag);
		return;
	}

	if (flag & (1 << CB_CMD_CREATE_SURFACE_SHIFT)) {
		clia_debug("receive create surface cmd");
		surface_create_proc(client, buf);
	}
	if (flag & (1 << CB_CMD_CREATE_VIEW_SHIFT)) {
		clia_debug("receive create view cmd");
		view_create_proc(client, buf);
	}
	if (flag & (1 << CB_CMD_CREATE_BO_SHIFT)) {
		clia_debug("receive create bo cmd");
		bo_create_proc(client, buf);
	}
	if (flag & (1 << CB_CMD_DESTROY_BO_SHIFT)) {
		clia_debug("receive destroy bo cmd");
		bo_destroy_proc(client, buf);
	}
	if (flag & (1 << CB_CMD_COMMIT_SHIFT)) {
		clia_debug("receive commit cmd");
		bo_commit_proc(client, buf);
	}
	if (flag & (1 << CB_CMD_DESTROY_SHIFT)) {
		clia_debug("receive destroy cmd");
	}
	if (flag & (1 << CB_CMD_SHELL_SHIFT)) {
		clia_debug("receive shell cmd");
		shell_proc(client, buf);
	}
	if (flag & (1 << CB_CMD_MC_COMMIT_SHIFT)) {
		clia_debug("receive mc commit cmd");
		if (client->capability & CB_CLIENT_CAP_MC) {
			mc_proc(client, buf);
		}
	}
}

static s32 client_agent_sock_cb(s32 fd, u32 mask, void *data)
{
	s32 ret, i;
	size_t byts_rd;
	struct cb_client_agent *client = data;
	s32 flag; /* 0: length not received, 1: length received. */
	struct cb_fds ipc_fds;
	s32 *p;

	if (client->cursor >= ((u8 *)(client->ipc_buf) + sizeof(size_t))) {
		flag = 1;
	} else {
		flag = 0;
	}

	do {
		/* bytes_to_rd is set when client create: sizeof(size_t) */
		/* printf("try to receive %lu\n", client->byts_to_rd); */
		ret = cb_recvmsg(client->sock, client->cursor,
				 client->byts_to_rd, &ipc_fds);
		/* printf("receive return %d\n", ret); */
	} while (ret == -EAGAIN);

	if (ret < 0) {
		clia_err("failed to recv client (%s).", strerror(-ret));
		client->c->rm_client(client->c, client);
		return ret;
	} else if (ret == 0) {
		clia_warn("connection lost.");
		client->c->rm_client(client->c, client);
		return 0;
	}

	if (ipc_fds.count > 0) {
		p = &client->ipc_fds.fds[client->ipc_fds.count];
		for (i = 0; i < ipc_fds.count; i++) {
			p[i] = ipc_fds.fds[i];
		}
		client->ipc_fds.count += ipc_fds.count;
	}

	client->cursor += ret;
	client->byts_to_rd -= ret;
	byts_rd = ret;

	if (!flag) {
		if (ret >= sizeof(size_t)) {
			/* received the length */
			flag = 1;
			memcpy(&client->byts_to_rd, client->ipc_buf,
			       sizeof(size_t));
			client->ipc_sz = client->byts_to_rd;
			/* printf("ipc size: %lu\n", client->ipc_sz); */
			/* printf("%p %p\n", client->cursor, client->ipc_buf);*/
			if ((byts_rd - sizeof(size_t)) > client->ipc_sz) {
				/* received more than one ipc message */
				client->byts_to_rd = 0;
			} else {
				client->byts_to_rd -= (byts_rd -sizeof(size_t));
			}
		}
	}

	if (!client->byts_to_rd) {
		client->cursor = (u8 *)client->ipc_buf;
		client->byts_to_rd = sizeof(size_t);
		/* printf("complete.\n"); */
		/* TODO &client->ipc_buf[0] + sizeof(size_t), ipc_sz */
		/*
		printf("proc ipc message: %p, %lu\n",
			&client->ipc_buf[0] + sizeof(size_t), client->ipc_sz);
		printf("ipc received %d fds.\n", client->ipc_fds.count);
		*/
		for (i = 0; i < client->ipc_fds.count; i++) {
			printf("fds[%d]: %d\n", i, client->ipc_fds.fds[i]);
		}
		ipc_proc(client);
		client->ipc_fds.count = 0;
	}

	return 0;
}

struct cb_client_agent *cb_client_agent_create(s32 sock,
					       struct compositor *c,
					       struct cb_event_loop *loop)
{
	struct cb_client_agent *client;
	u32 n;

	if (sock <= 0)
		return NULL;

	client = calloc(1, sizeof(*client));
	if (!client)
		goto err;

	memset(client, 0 ,sizeof(*client));
	client->sock = sock;
	client->loop = loop;

	client->sock_source = cb_event_loop_add_fd(client->loop, client->sock,
						   CB_EVT_READABLE,
						   client_agent_sock_cb,
						   client);
	if (!client->sock_source)
		goto err;

	client->byts_to_rd = sizeof(size_t);
	client->cursor = (u8 *)(client->ipc_buf);
	client->ipc_sz = 0;
	client->c = c;

	client->surface_id_created_tx_cmd_t
		= cb_server_create_surface_id_cmd(0, &n);
	assert(client->surface_id_created_tx_cmd_t);
	client->surface_id_created_tx_cmd = malloc(n);
	assert(client->surface_id_created_tx_cmd);
	client->surface_id_created_tx_len = n;

	client->view_id_created_tx_cmd_t
		= cb_server_create_view_id_cmd(0, &n);
	assert(client->view_id_created_tx_cmd_t);
	client->view_id_created_tx_cmd = malloc(n);
	assert(client->view_id_created_tx_cmd);
	client->view_id_created_tx_len = n;

	client->bo_id_created_tx_cmd_t
		= cb_server_create_bo_id_cmd(0, &n);
	assert(client->bo_id_created_tx_cmd_t);
	client->bo_id_created_tx_cmd = malloc(n);
	assert(client->bo_id_created_tx_cmd);
	client->bo_id_created_tx_len = n;

	client->bo_commit_ack_tx_cmd_t
		= cb_server_create_commit_ack_cmd(0, 0ULL, &n);
	assert(client->bo_commit_ack_tx_cmd_t);
	client->bo_commit_ack_tx_cmd = malloc(n);
	assert(client->bo_commit_ack_tx_cmd);
	client->bo_commit_ack_tx_len = n;

	client->bo_flipped_tx_cmd_t
		= cb_server_create_bo_flipped_cmd(0, 0ULL, &n);
	assert(client->bo_flipped_tx_cmd_t);
	client->bo_flipped_tx_cmd = malloc(n);
	assert(client->bo_flipped_tx_cmd);
	client->bo_flipped_tx_len = n;

	client->bo_complete_tx_cmd_t
		= cb_server_create_bo_complete_cmd(0, 0ULL, &n);
	assert(client->bo_complete_tx_cmd_t);
	client->bo_complete_tx_cmd = malloc(n);
	assert(client->bo_complete_tx_cmd);
	client->bo_complete_tx_len = n;

	client->hpd_tx_cmd_t
		= cb_server_create_hpd_cmd(NULL, &n);
	assert(client->hpd_tx_cmd_t);
	client->hpd_tx_cmd = malloc(n);
	assert(client->hpd_tx_cmd);
	client->hpd_tx_len = n;

	client->mc_commit_ack_tx_cmd_t
		= cb_server_create_mc_commit_ack_cmd(0, &n);
	assert(client->mc_commit_ack_tx_cmd_t);
	client->mc_commit_ack_tx_cmd = malloc(n);
	assert(client->mc_commit_ack_tx_cmd);
	client->mc_commit_ack_tx_len = n;

	client->shell_tx_cmd_t = cb_create_shell_cmd(0, &n);
	assert(client->shell_tx_cmd_t);
	client->shell_tx_cmd = malloc(n);
	assert(client->shell_tx_cmd);
	client->shell_tx_len = n;

	client->kbd_led_status_ack_cmd_t =
		cb_server_create_get_kbd_led_st_ack_cmd(0, &n);
	assert(client->kbd_led_status_ack_cmd_t);
	client->kbd_led_status_ack_cmd = malloc(n);
	assert(client->kbd_led_status_ack_cmd);
	client->kbd_led_status_ack_len = n;

	client->get_edid_ack_cmd_t =
		cb_server_create_get_edid_ack_cmd(0, NULL, 512, false, &n);
	assert(client->get_edid_ack_cmd_t);
	client->get_edid_ack_cmd = malloc(n);
	assert(client->get_edid_ack_cmd);
	client->get_edid_ack_len = n;

	client->view_focus_chg_cmd_t =
		cb_server_create_view_focus_chg_cmd(0ULL, false, &n);
	assert(client->view_focus_chg_cmd_t);
	client->view_focus_chg_cmd = malloc(n);
	assert(client->view_focus_chg_cmd);
	client->view_focus_chg_len = n;

	client->send_surface_ack = cb_client_agent_send_surface_ack;
	client->send_view_ack = cb_client_agent_send_view_ack;
	client->send_bo_create_ack = cb_client_agent_send_bo_create_ack;
	client->send_bo_commit_ack = cb_client_agent_send_bo_commit_ack;
	client->send_bo_flipped = cb_client_agent_send_bo_flipped;
	client->send_bo_complete = cb_client_agent_send_bo_complete;
	client->send_raw_input_evts = cb_client_agent_send_raw_input;
	client->send_hpd_evt = cb_client_agent_send_hpd_evt;
	client->send_mc_commit_ack = cb_client_agent_send_mc_commit_ack;
	client->send_shell_cmd = cb_client_agent_send_shell_cmd;
	client->destroy_pending = cb_client_agent_destroy_pending;
	client->send_view_focus_chg = cb_client_agent_send_view_focus_cfg;

	/* init surface list */
	INIT_LIST_HEAD(&client->surfaces);
	/* init buffer list */
	INIT_LIST_HEAD(&client->buffers);

	return client;

err:
	cb_client_agent_destroy(client);
	return NULL;
}

