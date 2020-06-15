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
 * socket.c -- entry points for librpma socket
 */

#include <unistd.h>

#include <librpma.h>

#include "alloc.h"
#include "socket.h"
#include "rpma_utils.h"
#include "info.h"
#include "peer.h"
#include "connection.h"

static void
socket_dump(struct rpma_info *info)
{
	const char *dump = info_dump(info);
	ASSERTne(dump, NULL);
	fprintf(stderr, "Started listening on %s\n", dump);
}

static int
socket_init(struct rpma_socket *socket, const char *addr, const char *service)
{
	struct rpma_info info;
	info.addr = addr;
	info.service = service;
	info.passive = RPMA_INFO_PASSIVE;

	/* translate address */
	int ret = info_resolve(&info);
	if (ret)
		return ret;

	/* create a new RDMA id */
	ret = rdma_create_id(socket->ec.rdma_ec, &socket->id, NULL, RDMA_PS_TCP);
	if (ret) {
		ret = RPMA_E_ERRNO;
		goto err_create_id;
	}

	/* binad address */
	ret = rdma_bind_addr(socket->id, info.rai->ai_src_addr);
	if (ret) {
		ret = RPMA_E_ERRNO;
		goto err_bind_addr;
	}

	/* check the socket is using the same device as its peer */
	ASSERTeq(socket->id->verbs, socket->peer->pd->context);

	ret = rpma_utils_ec_poll_new(&socket->ec);
	if (ret)
		goto err_ec_poll_new;

	/* attach RDMA id to the event channel */
	ret = rdma_migrate_id(socket->id, socket->ec.rdma_ec);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_migrate_id");
		goto err_migrate_id;
	}

	/* start listening */
	ret = rdma_listen(socket->id, 0 /* backlog */);
	if (ret) {
		ret = RPMA_E_ERRNO;
		goto err_listen;
	}

	/* dump & release address translation resources */
	socket_dump(&info);
	info_free(&info);

	return 0;

err_listen:
err_migrate_id:
	(void)rpma_utils_ec_poll_delete(&socket->ec);
err_ec_poll_new:
err_bind_addr:
	rdma_destroy_id(socket->id);
	socket->id = NULL;
err_create_id:
	info_free(&info);
	return ret;
}

static void
socket_fini(struct rpma_socket *socket)
{
	ASSERTne(socket->id, NULL);

	int ret = rdma_destroy_id(socket->id);
	ASSERTeq(ret, 0);

	ret = rpma_utils_ec_poll_delete(&socket->ec);
	ASSERTeq(ret, 0);
}

int
rpma_listen(struct rpma_peer *peer, struct rpma_conn_cfg *cfg,
		const char *addr, const char *service, struct rpma_socket **socket)
{
	struct rpma_socket *tmp = Malloc(sizeof(*tmp));
	if (!tmp)
		return RPMA_E_ERRNO;

	tmp->peer = peer;
	memcpy(&tmp->cfg, cfg, sizeof(*cfg));
	tmp->ec.epoll = RPMA_FD_INVALID;
	tmp->ec.rdma_ec = NULL;
	tmp->id = NULL;
	tmp->edata = NULL;

	int ret = socket_init(tmp, addr, service);
	if (ret)
		goto err_socket_init;

	*socket = tmp;

	return 0;

err_socket_init:
	Free(tmp);
	return ret;
}

int
rpma_socket_fd(struct rpma_socket *socket)
{
	return socket->ec.rdma_ec->fd;
}

int
rpma_accept(struct rpma_socket *socket, struct rpma_conn **conn)
{
	/* get an event */
	int ret = rdma_get_cm_event(socket->ec.rdma_ec, &socket->edata);
	if (ret)
		return RPMA_E_ERRNO;

	/* we expect only one type of event here */
	ASSERTeq(socket->edata->event, RDMA_CM_EVENT_CONNECT_REQUEST);

	struct rpma_conn *tmp = NULL;
	ret = rpma_conn_new(socket->peer, &socket->cfg, &tmp);
	if (ret)
		goto err_conn_new;

	/* initialize RMA & MSG resources */
	ret = rpma_conn_init(tmp);
	if (ret)
		goto err_conn_init;

	/* create CQ and QP for RDMA id */
	ret = rpma_conn_id_init(tmp, socket->edata->id);
	if (ret)
		goto err_conn_id_init;

	/* post RECVs */
	ret = rpma_conn_recv_post_all(tmp);
	if (ret)
		goto err_recv_post_all;

	struct rdma_conn_param *conn_param = rpma_conn_param();

	/* XXX reject logic */
	ret = rdma_accept(tmp->id, conn_param);
	if (ret) {
		ERR_STR(ret, "rdma_accept");
		goto err_accept;
	}

	/* ack RDMA_CM_EVENT_CONNECT_REQUEST */
	ret = rdma_ack_cm_event(socket->edata);
	if (ret)
		goto err_event_ack;

	/* XXXXXXXXXXXXXXX TEST ZONE */

	struct rdma_event_channel *evch = rdma_create_event_channel();
	ASSERTne(evch, NULL);

	ASSERTeq(rdma_migrate_id(tmp->id, evch), 0);

	/* wait for an event */
//	ret = rpma_utils_ec_poll_wait(&socket->ec, socket->cfg.setup_timeout);
//	if (ret)
//		goto err_wait_event;

	/* get the event */
	ret = rdma_get_cm_event(evch, &socket->edata);
	if (ret) {
		ret = RPMA_E_ERRNO;
		goto err_event;
	}

	/* we expect only one type of event here */
	ASSERTeq(socket->edata->event, RDMA_CM_EVENT_ESTABLISHED);

	/* ack RDMA_CM_EVENT_ESTABLISHED */
	ret = rdma_ack_cm_event(socket->edata);
	if (ret)
		goto err_event_ack2;

	/* create epollable event channel */
	ret = rpma_utils_ec_poll_new(&tmp->ec);
	if (ret)
		goto err_ec_poll_new;

	/* attach RDMA id to the event channel */
	ret = rdma_migrate_id(tmp->id, tmp->ec.rdma_ec);
	if (ret) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "rdma_migrate_id");
		goto err_migrate_id;
	}

	/* XXXXXXXXXXXXXXX TEST ZONE END */

	*conn = tmp;

	return 0;

err_migrate_id:
	(void)rpma_utils_ec_poll_delete(&tmp->ec);
err_ec_poll_new:
	/* XXX rdma_ack_cm_event */
err_event_ack2:
err_event:
err_wait_event:
err_event_ack:
err_accept:
err_recv_post_all:
	rpma_conn_id_fini(tmp);
err_conn_id_init:
	rpma_conn_id_fini(tmp);
err_conn_init:
	Free(tmp);
err_conn_new:
	(void)rdma_ack_cm_event(socket->edata);
	return ret;
}

int
rpma_close(struct rpma_socket **socket)
{
	socket_fini(*socket);

	Free(*socket);
	*socket = NULL;
	return 0;
}
