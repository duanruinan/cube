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
#include <fcntl.h>
#include <unistd.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <errno.h>
#include <cube_utils.h>
#include <cube_log.h>
#include <cube_event.h>
#include <cube_shm.h>

s32 cb_shm_init(struct cb_shm *shm, const char *shm_id, u32 size, s32 creator)
{
	if (creator)
		shm_unlink(shm_id);

	memset(shm, 0, sizeof(*shm));
	strcpy(shm->name, shm_id);
	shm->sz = size;

	shm->creator = creator;
	if (shm->creator)
		shm->fd = shm_open(shm->name,
				   O_CREAT | O_RDWR, S_IRUSR | S_IWUSR);
	else
		shm->fd = shm_open(shm->name, O_RDWR, S_IRUSR | S_IWUSR);
	if (shm->fd < 0) {
		fprintf(stderr, "shm_open %s fail. %m", shm_id);
		return -errno;
	}

	if (cb_set_cloexec_or_close(shm->fd) < 0)
		fprintf(stderr, "failed to set shm fd cloexec.");

	ftruncate(shm->fd, shm->sz);
	shm->map = mmap(NULL, shm->sz, PROT_READ | PROT_WRITE,
			MAP_SHARED, shm->fd, 0);

	if (!shm->map)
		return -errno;

	return 0;
}

void cb_shm_release(struct cb_shm *shm)
{
	if (shm->map)
		munmap(shm->map, shm->sz);

	shm->map = NULL;

	if (shm->fd)
		close(shm->fd);

	shm->fd = 0;
	shm->sz = 0;

	if (shm->creator)
		shm_unlink(shm->name);

	shm->creator = 0;

	memset(shm->name, 0, CB_SHM_NM_MAX_LEN);
}

