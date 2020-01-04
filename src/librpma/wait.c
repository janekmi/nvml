/*
 * Copyright 2019-2020, Intel Corporation
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
 * wait.c -- entry points for librpma wait
 */

#include <infiniband/verbs.h>

#include <librpma.h>

#include "connection.h"
#include "rpma_utils.h"

static inline int
cq_read(struct rpma_conn *conn, struct ibv_wc *wc)
{
	int ret = ibv_poll_cq(conn->cq, 1 /* num_entries */, wc);
	if (ret == 0)
		return 0;
	if (ret < 0) {
		ERR_STR(ret, "ibv_poll_cq");
		return ret;
	}

	ASSERTeq(ret, 1);
	ASSERTeq(wc->status, IBV_WC_SUCCESS); /* XXX */

	return ret;
}

/* XXX rpma_poll? */

int
rpma_wait(struct rpma_conn *conn, void **op_context)
{
	int ret = 0;

	do {
		ret = cq_read(conn, &conn->wc);
		if (ret < 0)
			return ret;
		else if (ret == 0)
			continue;

		/* XXX RPMA_OP_COMMIT ? */

		switch (conn->wc.opcode) {
		case IBV_WC_SEND:
			ret = 0; /* XXX */
			break;
		case IBV_WC_RDMA_WRITE:
			ret = RPMA_OP_WRITE;
			break;
		case IBV_WC_RDMA_READ:
			ret = RPMA_OP_READ;
			break;
		case IBV_WC_RECV:
			ret = RPMA_OP_RECV;
			break;
		default:
			ret = 0;
		}

		if (ret && op_context)
			*op_context = (void *)conn->wc.wr_id;

	} while (ret == 0);

	return ret;
}
