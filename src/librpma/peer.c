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
 * peer.c -- entry points for librpma peer
 */

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/epoll.h>
#include <unistd.h>

#include <librpma.h>

#include "ravl.h"
#include "device.h"
#include "peer.h"
#include "configs.h"
#include "rpma_utils.h"
#include "alloc.h"
#include "valgrind_internal.h"
#include "connection.h"

// #define RX_TX_SIZE 256 /* XXX */

//struct id_conn_pair
//{
//	struct rdma_cm_id *id;
//	struct rpma_conn *conn;
//};
//
//static int
//id_conn_pair_compare(const void *lhs, const void *rhs)
//{
//	const struct id_conn_pair *l = lhs;
//	const struct id_conn_pair *r = rhs;
//
//	intptr_t diff = (intptr_t)l->id - (intptr_t)r->id;
//	if (diff != 0)
//		return diff > 0 ? 1 : -1;
//
//	return 0;
//}

int
rpma_peer_new(struct rpma_peer_cfg *zcfg, struct rpma_device *device,
		struct rpma_peer **peer)
{
	int ret = 0;

	struct rpma_peer *ptr = Malloc(sizeof(struct rpma_peer));
	if (!ptr)
		return RPMA_E_ERRNO;

	ptr->pd = NULL;
//	ptr->active_connections = 0;
//	ptr->connections = ravl_new(id_conn_pair_compare);

	/* protection domain */
	ptr->pd = ibv_alloc_pd(device->verbs);
	if (!ptr->pd) {
		ret = RPMA_E_UNKNOWN; /* XXX */
		goto err_alloc_pd;
	}

	*peer = ptr;

	return ret;

err_alloc_pd:
	Free(ptr);
	return ret;
}

int
rpma_peer_delete(struct rpma_peer **peer)
{
	struct rpma_peer *ptr = *peer;
	if (!ptr)
		return 0;

	if (ptr->pd)
		ibv_dealloc_pd(ptr->pd);

	Free(ptr);
	*peer = NULL;

	return 0;
}

//void
//rpma_peer_conn_store(struct ravl *store, struct rpma_conn *conn)
//{
//	struct id_conn_pair *pair = Malloc(sizeof(*pair));
//	pair->id = conn->id;
//	pair->conn = conn;
//
//	ravl_insert(store, pair);
//}

//struct rpma_conn *
//rpma_peer_conn_restore(struct ravl *store, struct rdma_cm_id *id)
//{
//	struct id_conn_pair to_find;
//	to_find.id = id;
//	to_find.conn = NULL;
//
//	struct ravl_node *node = ravl_find(store, &to_find, RAVL_PREDICATE_EQUAL);
//	if (!node)
//		return NULL;
//
//	struct id_conn_pair *found = ravl_data(node);
//	if (!found)
//		return NULL;
//
//	struct rpma_conn *ret = found->conn;
//	Free(found);
//	ravl_remove(store, node);
//
//	return ret;
//}
