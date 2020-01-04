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
 * connection.h -- internal definitions for librpma connection
 */
#ifndef RPMA_CONN_H
#define RPMA_CONN_H

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include <librpma.h>

#include "configs.h"
#include "rpma_utils.h"

struct rpma_rma {
	struct rpma_memory *raw_dst;
	struct rpma_memory_remote *raw_src;

	struct ibv_sge sge;
	struct ibv_send_wr wr;
};

struct rpma_msg {
	struct rpma_memory *buff;

	struct ibv_sge sge;
	union {
		struct ibv_recv_wr recv;
		struct ibv_send_wr send;
	};
};

struct rpma_conn {
	struct rpma_peer *peer;
	struct rpma_conn_cfg cfg;

	struct ec_poll ec;
	struct rdma_cm_id *id;

	struct ibv_cq *cq;
	struct ibv_wc wc;

	struct rpma_rma rma;

	struct rpma_msg send;
	struct rpma_msg recv;
	uint64_t send_buff_id;

	void *app_context;
};

int rpma_conn_new(struct rpma_peer *peer, struct rpma_conn_cfg *cfg,
		struct rpma_conn **conn);

int rpma_conn_init(struct rpma_conn *conn);
int rpma_conn_fini(struct rpma_conn *conn);

int rpma_conn_id_init(struct rpma_conn *conn, struct rdma_cm_id *id);
int rpma_conn_id_fini(struct rpma_conn *conn);

int rpma_conn_rma_init(struct rpma_conn *conn);
int rpma_conn_rma_fini(struct rpma_conn *conn);

int rpma_conn_msg_init(struct rpma_conn *conn);
int rpma_conn_msg_fini(struct rpma_conn *conn);

struct rdma_conn_param *rpma_conn_param(void);
int rpma_conn_recv_post_all(struct rpma_conn *conn);

#endif /* RPMA_CONN_H */
