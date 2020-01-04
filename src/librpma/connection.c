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
 * connection.c -- entry points for librpma connection
 */

#include <rdma/rdma_cma.h>

#include <librpma.h>

#include "alloc.h"
#include "connection.h"
#include "memory.h"
#include "info.h"
#include "rpma_utils.h"
#include "peer.h"

int
rpma_conn_id_init(struct rpma_conn *conn, struct rdma_cm_id *id)
{
	struct rpma_peer *peer = conn->peer;
	int ret = 0;

	/* create CQ */
	conn->cq = ibv_create_cq(id->verbs, CQ_SIZE, (void *)conn, 0, 0);
	if (!conn->cq)
		return RPMA_E_ERRNO;

	/* preapre QP attributes */
	struct ibv_qp_init_attr init_qp_attr;
	init_qp_attr.qp_context = conn;
	init_qp_attr.send_cq = conn->cq;
	init_qp_attr.recv_cq = conn->cq;
	init_qp_attr.srq = NULL;
	init_qp_attr.cap.max_send_wr = CQ_SIZE; /* XXX */
	init_qp_attr.cap.max_recv_wr = CQ_SIZE; /* XXX */
	init_qp_attr.cap.max_send_sge = 1;
	init_qp_attr.cap.max_recv_sge = 1;
	init_qp_attr.cap.max_inline_data = 0; /* XXX */
	init_qp_attr.qp_type = IBV_QPT_RC;
	init_qp_attr.sq_sig_all = 0;

	/* create QP */
	ret = rdma_create_qp(id, peer->pd, &init_qp_attr);
	if (ret) {
		ret = RPMA_E_ERRNO;
		goto err_create_qp;
	}

	conn->id = id;
	return 0;

err_create_qp:
	ibv_destroy_cq(conn->cq);
	conn->cq = NULL;
	return ret;
}

int
rpma_conn_id_fini(struct rpma_conn *conn)
{
	int ret = 0;

	/* nothing to do */
	if (!conn->id)
		return 0;

	/* destroy QP */
	ASSERTne(conn->id->qp, NULL);
	rdma_destroy_qp(conn->id);

	if (conn->cq) {
		ret = ibv_destroy_cq(conn->cq);
		if (ret) {
			ERR_STR(ret, "ibv_destroy_cq");
			return -ret; /* XXX macro? */
		}
		conn->cq = NULL;
	}

	return 0;
}

int
rpma_conn_recv_post_all(struct rpma_conn *conn)
{
	int ret;
	void *ptr = conn->recv.buff->ptr;
	void *ptr_cpy = ptr;

	for (uint64_t i = 0; i < conn->cfg.recv_buffers_num; ++i) {
		ret = rpma_recv_buffer_return(conn, &ptr_cpy);
		if (ret)
			return ret;
		ptr = (void *)((uintptr_t)ptr + conn->cfg.msg_size);
		ptr_cpy = ptr;
	}

	return 0;
}

struct rdma_conn_param *
rpma_conn_param()
{
	static struct rdma_conn_param conn_param;
	memset(&conn_param, 0, sizeof conn_param);
	conn_param.private_data = NULL; /* XXX very interesting */
	conn_param.private_data_len = 0;
	conn_param.responder_resources = RDMA_MAX_RESP_RES;
	conn_param.initiator_depth = RDMA_MAX_INIT_DEPTH;
	conn_param.flow_control = 1;
	conn_param.retry_count = 7; /* max 3-bit value */
	conn_param.rnr_retry_count = 7; /* max 3-bit value */
	/* since QP is created on this connection id srq and qp_num are ignored */

	return &conn_param;
}

static int
conn_connect(struct rpma_conn *conn, const char *addr, const char *service)
{
	struct rpma_info info;
	info.addr = addr;
	info.service = service;
	info.passive = RPMA_INFO_ACTIVE;

	/* translate address */
	int ret = info_resolve(&info);
	if (ret)
		return ret;

	/* create a new RDMA id */
	ret = rdma_create_id(NULL, &conn->id, NULL, RDMA_PS_TCP);
	if (ret) {
		ret = RPMA_E_ERRNO;
		goto err_create_id;
	}

	/* resolve address */
	ret = rdma_resolve_addr(conn->id, info.rai->ai_src_addr, info.rai->ai_dst_addr,
			RPMA_DEFAULT_TIMEOUT);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_resolve_addr");
		goto err_resolve_addr;
	}

	/* resolve route */
	ret = rdma_resolve_route(conn->id, RPMA_DEFAULT_TIMEOUT);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_resolve_route");
		goto err_resolve_route;
	}

	/* create CQ and QP for RDMA id */
	ret = rpma_conn_id_init(conn, conn->id);
	if (ret)
		goto err_id_init;

	/* post RECVs */
	ret = rpma_conn_recv_post_all(conn);
	if (ret)
		goto err_recv_post_all;

	/* RDMA connection params */
	struct rdma_conn_param *conn_param = rpma_conn_param();

	/* connect */
	ret = rdma_connect(conn->id, conn_param);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_connect");
		goto err_connect;
	}

	/* create epollable event channel */
	ret = rpma_utils_ec_poll_new(&conn->ec);
	if (ret)
		goto err_ec_poll_new;

	/* attach RDMA id to the event channel */
	ret = rdma_migrate_id(conn->id, conn->ec.rdma_ec);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_migrate_id");
		goto err_migrate_id;
	}

	/* release address translation resources */
	info_free(&info);

	return 0;

err_migrate_id:
	(void)rpma_utils_ec_poll_delete(&conn->ec);
err_ec_poll_new:
	(void)rdma_disconnect(conn->id);
err_connect:
err_recv_post_all:
	(void)rpma_conn_id_fini(conn);
err_id_init:
err_resolve_route:
err_resolve_addr:
	(void)rdma_destroy_id(conn->id);
	conn->id = NULL;
err_create_id:
	info_free(&info);
	return ret;
}

static int
conn_disconnect(struct rpma_conn *conn)
{
	int ret = rpma_utils_ec_poll_delete(&conn->ec);
	ASSERTeq(ret, 0);

	ret = rpma_conn_id_fini(conn);
	ASSERTeq(ret, 0);

	ret = rpma_conn_fini(conn);
	ASSERTeq(ret, 0);

	return 0;
}

int
rpma_conn_init(struct rpma_conn *conn)
{
	/* initialize RMA resources */
	int ret = rpma_conn_rma_init(conn);
	if (ret)
		return ret;

	/* initialize MSG resources */
	ret = rpma_conn_msg_init(conn);
	if (ret)
		goto err_msg_init;

	return 0;

err_msg_init:
	(void)rpma_conn_rma_fini(conn);
	return ret;
}

int
rpma_conn_fini(struct rpma_conn *conn)
{
	/* release MSG resources */
	int ret = rpma_conn_msg_fini(conn);
	if (ret)
		goto err_msg_fini;

	/* release RMA resources */
	ret = rpma_conn_rma_fini(conn);
	if (ret)
		return ret;

	return 0;

err_msg_fini:
	(void)rpma_conn_rma_fini(conn);
	return ret;
}

int
rpma_conn_new(struct rpma_peer *peer, struct rpma_conn_cfg *cfg,
		struct rpma_conn **conn)
{
	struct rpma_conn *tmp = Malloc(sizeof(*tmp));
	if (!tmp)
		return RPMA_E_ERRNO;

	/* zero out RPMA connection */
	tmp->peer = peer;
	memcpy(&tmp->cfg, cfg, sizeof(*cfg));
	tmp->ec.epoll = RPMA_FD_INVALID;
	tmp->ec.rdma_ec = NULL;
	tmp->id = NULL;
	tmp->cq = NULL;
	tmp->wc.status = IBV_WC_GENERAL_ERR;
	memset(&tmp->rma, 0, sizeof(tmp->rma));
	memset(&tmp->send, 0, sizeof(tmp->send));
	memset(&tmp->recv, 0, sizeof(tmp->recv));
	tmp->send_buff_id = 0;
	tmp->app_context = NULL;

	*conn = tmp;

	return 0;
}

int
rpma_connect(struct rpma_peer *peer, struct rpma_conn_cfg *cfg,
		const char *addr, const char *service, struct rpma_conn **conn)
{
	struct rpma_conn *tmp = NULL;
	int ret = rpma_conn_new(peer, cfg, &tmp);
	if (ret)
		return ret;

	/* initialize RMA & MSG resources */
	/* XXX move to conn_connect? */
	ret = rpma_conn_init(tmp);
	if (ret)
		goto err_conn_init;

	/* connect */
	ret = conn_connect(tmp, addr, service);
	if (ret)
		goto err_connect;

	*conn = tmp;

	return 0;

err_connect:
	(void)rpma_conn_fini(tmp);
err_conn_init:
	Free(tmp);
	return ret;
}

int
rpma_conn_set_app_context(struct rpma_conn *conn, void *data)
{
	conn->app_context = data;
	return 0;
}

int
rpma_conn_get_app_context(struct rpma_conn *conn, void **data)
{
	*data = conn->app_context;
	return 0;
}

static int
conn_wait_disconnected(struct rpma_conn *conn)
{
	/* wait for the incoming event */
	int ret = rpma_utils_ec_poll_wait(&conn->ec, conn->cfg.setup_timeout);
	if (ret)
		return ret;

	/* get an event from the event channel */
	struct rdma_cm_event *edata;
	ret = rdma_get_cm_event(conn->ec.rdma_ec, &edata);
	if (ret)
		ASSERT(0);

	/* we expect here only a one type of event */
	ASSERTeq(edata->event, RDMA_CM_EVENT_DISCONNECTED);

	/* ACK event before return */
	ret = rdma_ack_cm_event(edata);
	if (ret)
		ASSERT(0);

	return 0;
}

int
rpma_disconnect(struct rpma_conn **conn, int flags)
{
	struct rpma_conn *ptr = *conn;
	int ret;

	/* wait for the disconnect on the remote side */
	if (flags & RPMA_DISCONNECT_WHEN_DONE) {
		ret = conn_wait_disconnected(ptr);
		if (ret)
			return ret;
	}

	/*
	 * disconnect when ready either:
	 * - RPMA_DISCONNECT_NOW - without waiting
	 * - remote side has already disconnected
	 */
	ASSERTne(ptr->id, NULL);
	/* XXX any prior messaging? */
	ret = rdma_disconnect(ptr->id);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_disconnect");
		return ret;
	}

	/*
	 * !RPMA_DISCONNECT_WHEN_DONE == RPMA_DISCONNECT_NOW
	 * so we have to wait for the confirmation from the remote side
	 */
	if (!(flags & RPMA_DISCONNECT_WHEN_DONE)) {
		ret = conn_wait_disconnected(ptr);
		ASSERTeq(ret, 0);
	}

	ret = conn_disconnect(ptr);
	ASSERTeq(ret, 0);

	Free(ptr);
	*conn = NULL;

	return ret;
}
