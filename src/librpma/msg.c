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
 * msg.c -- entry points for librpma MSG
 */

#include <errno.h>

#include "alloc.h"
#include "connection.h"
#include "memory.h"
#include "util.h"
#include "rpma_utils.h"

static int
msg_queue_init(struct rpma_conn *conn, size_t queue_length,
		int access, struct rpma_memory **buff)
{
	/* calculate buffer size */
	size_t buff_size = conn->cfg.msg_size * queue_length;
	buff_size = ALIGN_UP(buff_size, Pagesize);

	/* allocate buffer */
	void *ptr;
	errno = posix_memalign((void **)&ptr, Pagesize, buff_size);
	if (errno)
		return RPMA_E_ERRNO;

	/* register the memory for RDMA use */
	int ret = rpma_memory_new_internal(conn->peer, ptr, buff_size,
			access, buff);
	if (ret)
		goto err_mem_local_new;

	return 0;

err_mem_local_new:
	Free(ptr);
	return ret;
}

/* XXX make it common with raw_buffer_init/_fini ? */
static int
msg_queue_fini(struct rpma_conn *conn, struct rpma_memory **buff)
{
	void *ptr = (*buff)->ptr;

	int ret = rpma_memory_delete(buff);
	if (ret)
		return ret;

	Free(ptr);

	return 0;
}

static void
msg_init(struct ibv_send_wr *send, struct ibv_recv_wr *recv,
		struct ibv_sge *sge, struct rpma_memory *buff, size_t length)
{
	ASSERT(length < UINT32_MAX);
	ASSERTne(sge, NULL);

	/* initialize either send or recv messages */
	if (send) {
		ASSERTeq(recv, NULL);

		memset(send, 0, sizeof(*send));
		send->wr_id = 0;
		send->next = NULL;
		send->sg_list = sge;
		send->num_sge = 1;
		send->opcode = IBV_WR_SEND;
		send->send_flags = IBV_SEND_SIGNALED;
	} else {
		ASSERTne(recv, NULL);

		memset(recv, 0, sizeof(*recv));
		recv->next = NULL;
		recv->sg_list = sge;
		recv->num_sge = 1;
		recv->wr_id = 0;
	}

	/* sge->addr has to be provided just before ibv_post_send */
	sge->length = (uint32_t)length;
	sge->lkey = buff->mr->lkey;
}

int
rpma_conn_msg_init(struct rpma_conn *conn)
{
	struct rpma_msg *send = &conn->send;
	struct rpma_msg *recv = &conn->recv;

	int ret;

	conn->send_buff_id = 0;

	/* allocate and register buffer for RDMA */
	int msg_access = IBV_ACCESS_LOCAL_WRITE; /* XXX ? */
	ret = msg_queue_init(conn, conn->cfg.send_buffers_num, msg_access,
			&send->buff);
	if (ret)
		return ret;

	ret = msg_queue_init(conn, conn->cfg.recv_buffers_num, msg_access,
			&recv->buff);
	if (ret)
		goto err_recv_queue_init;

	/* initialize RDMA messages */
	msg_init(
			&send->send,
			NULL,
			&send->sge, send->buff, conn->cfg.msg_size);
	msg_init(
			NULL,
			&recv->recv,
			&recv->sge, recv->buff, conn->cfg.msg_size);

	return 0;

err_recv_queue_init:
	(void)msg_queue_fini(conn, &conn->send.buff);
	return ret;
}

int
rpma_conn_msg_fini(struct rpma_conn *conn)
{
	int ret;
	ret = msg_queue_fini(conn, &conn->recv.buff);
	if (ret)
		return ret;

	ret = msg_queue_fini(conn, &conn->send.buff);
	if (ret)
		return ret;

	return 0;
}

int
rpma_send_buffer_get(struct rpma_conn *conn, size_t buff_size,
		void **buff)
{
	ASSERT(buff_size <= conn->cfg.msg_size);

	/* get base pointer for the SEND message buffer */
	void *tmp;
	int ret = rpma_memory_get_ptr(conn->send.buff, &tmp);
	if (ret)
		return ret;

	/* calculate next buffer id */
	uint64_t buff_id = conn->send_buff_id;
	conn->send_buff_id = (buff_id + 1) % conn->cfg.send_buffers_num;

	/* calculcate exact buffer pointer */
	tmp = (void *)((uintptr_t)tmp + buff_id * conn->cfg.msg_size);

	/* zero out buffer before giving it to the user */
	memset(tmp, 0, conn->cfg.msg_size);

	*buff = tmp;

	return 0;
}

int
rpma_send(struct rpma_conn *conn, void *ptr)
{
	struct ibv_send_wr *bad_wr;
	uint64_t addr = (uint64_t)ptr;

	/* prepare SEND message */
	struct rpma_msg *msg = &conn->send;
	msg->send.wr_id = addr;
	msg->sge.addr = addr;

	/* send the message */
	int ret = ibv_post_send(conn->id->qp, &msg->send, &bad_wr);
	if (ret) {
		ERR_STR(ret, "ibv_post_send");
		return ret;
	}

	/* XXX we need to track IBV_WC_SEND to release send buffers for later use */

	return 0;
}

int
rpma_recv_buffer_get(struct rpma_conn *conn, size_t *buff_size, void **buff)
{
	ASSERTeq(conn->wc.status, IBV_WC_SUCCESS);
	ASSERTeq(conn->wc.opcode, IBV_WC_RECV);

	*buff = (void *)conn->wc.wr_id;
	*buff_size = (size_t)conn->wc.byte_len;

	return 0;
}

int
rpma_recv_buffer_return(struct rpma_conn *conn, void **buff)
{
	/* ASSERT / if *buff is in RECV buffer range */

	struct ibv_recv_wr *bad_wr;
	uint64_t addr = (uint64_t)*buff;

	/* prepare RECV message */
	struct rpma_msg *msg = &conn->recv;
	msg->recv.wr_id = addr;
	msg->sge.addr = addr;

	/* post the RECV messages */
	int ret = ibv_post_recv(conn->id->qp, &msg->recv, &bad_wr);
	if (ret)
		return -ret; /* XXX macro? */

	/* zero out the user pointer just in case */
	*buff = NULL;

	return 0;
}
