/*
 * Copyright 2019, Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in
 *       the documentation and/or other materials provided with the
 *       distribution.
 *
 *     * Neither the name of the copyright holder nor the names of its
 *       contributors may be used to endorse or promote products derived
 *       from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * dispatcher.c -- entry points for librpma dispatcher
 */

#include "dispatcher.h"
#include "rpma_utils.h"

static int
dispatcher_init(struct rpma_dispatcher *disp)
{
	int ret;

	struct fi_poll_attr attr;
	attr.flags = 0;

	ret = fi_poll_open(disp->zone->domain, &attr, &disp->pollset);
	if (ret) {
		ERR_FI(ret, "fi_poll_open");
		return ret;
	}

	return 0;
}

static void
dispatcher_fini(struct rpma_dispatcher *disp)
{
	rpma_utils_res_close(&disp->pollset, "pollset");
}

int
rpma_dispatcher_new(struct rpma_zone *zone, struct rpma_dispatcher **disp)
{
	struct rpma_dispatcher *ptr = Malloc(sizeof(*ptr));
	if (!ptr)
		return RPMA_E_ERRNO;

	ptr->zone = zone;

	int ret = dispatcher_init(*ptr);
	if (ret)
		goto err_init;

	*disp = ptr;

	return 0;

err_init:
	Free(ptr);
	return ret;
}

int
rpma_dispatcher_delete(struct rpma_dispatcher **ptr)
{
	struct rpma_dispatcher *disp = *ptr;
	dispatcher_fini(disp);

	Free(disp);
	*ptr = NULL;

	return 0;
}

int
rpma_dispatcher_attach_connection(struct rpma_dispatcher *disp,
		struct rpma_connection *conn)
{
	int ret;
	uint64_t flags = 0;
	ret = fi_poll_add(disp->pollset, conn->cq->fid, flags);
	if (ret) {
		ERR_FI(ret, "fi_poll_add");
		return ret;
	}

	return 0;
}

int
rpma_dispatcher_detach_connection(struct rpma_dispatcher *disp,
		struct rpma_connection *conn)
{
	int ret;
	uint64_t flags = 0;
	ret = fi_poll_del(disp->pollset, conn->cq->fid, flags);
	if (ret) {
		ERR_FI(ret, "fi_poll_del");
		return ret;
	}

	return 0;
}

static int
dispatcher_is_waiting(struct rpma_dispatcher *disp)
{
	int breaking;
	util_atomic_load_explicit32(&disp->wait_breaking, &breaking, memory_order_acquire);
	return !breaking;
}

int
rpma_dispatch(struct rpma_dispatcher *disp)
{
	int ret;
	void *context;
	int count = 1;
	struct rpma_connection *conn;

	while (dispatcher_is_waiting(disp)) {
		ret = fi_poll(disp->pollset, &context, count);

		if (ret == 0)
			continue;

		if (ret < 0) {
			ERR_FI(ret, "fi_poll");
			return ret;
		}

		ASSERTeq(ret, count);

		conn = context;

		/* XXX rpma_connection_cq_wait() */
	}

	return 0;
}
