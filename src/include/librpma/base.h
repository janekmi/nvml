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
 * librpma/base.h -- base definitions of librpma entry points (EXPERIMENTAL)
 *
 * This library provides low-level support for remote access to persistent
 * memory utilizing RDMA-capable RNICs.
 *
 * See librpma(7) for details.
 */

#ifndef LIBRPMA_BASE_H
#define LIBRPMA_BASE_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* XXX rework in pmem2-style */
#define RPMA_E_UNKNOWN				(-100000)
#define RPMA_E_NOSUPP				(-100001)
#define RPMA_E_NEGATIVE_TIMEOUT		(-100002)
#define RPMA_E_NOT_LISTENING		(-100003)
#define RPMA_E_EC_READ				(-100004)
#define RPMA_E_EC_EVENT				(-100005)
#define RPMA_E_EC_EVENT_DATA		(-100006)
#define RPMA_E_UNHANDLED_EVENT		(-100007)
#define RPMA_E_UNKNOWN_CONNECTION	(-100008)
#define RPMA_E_TIMEOUT				(-100009)


/* picking up an RDMA-capable device */
struct rpma_device;

int rpma_device_by_src_address(const char *addr, struct rpma_device **rdev);

int rpma_device_by_dst_address(const char *addr, struct rpma_device **rdev);

int rpma_device_delete(struct rpma_device **rdev);


/* an RPMA peer config */
struct rpma_peer_cfg;

int rpma_peer_cfg_new(struct rpma_peer_cfg **zcfg);

int rpma_peer_cfg_delete(struct rpma_peer_cfg **zcfg);


/* peer */
struct rpma_peer;

int rpma_peer_new(struct rpma_peer_cfg *zcfg, struct rpma_device *rdev,
		struct rpma_peer **peer);

int rpma_peer_delete(struct rpma_peer **peer);


/* connection config setup */
struct rpma_conn_cfg;

int rpma_conn_cfg_new(struct rpma_conn_cfg **cfg);

typedef void *(*rpma_malloc_func)(size_t size);

typedef void (*rpma_free_func)(void *ptr);

int rpma_conn_cfg_set_msg_buffer_alloc_funcs(struct rpma_conn_cfg *cfg,
		rpma_malloc_func malloc_func, rpma_free_func free_func);

int rpma_conn_cfg_set_max_msg_size(struct rpma_conn_cfg *cfg, size_t msg_size);

int rpma_conn_cfg_set_send_buffers_num(struct rpma_conn_cfg *cfg,
		uint64_t buff_num);

int rpma_conn_cfg_set_recv_buffers_num(struct rpma_conn_cfg *cfg,
		uint64_t buff_num);

int rpma_conn_cfg_set_setup_timeout(struct rpma_conn_cfg *cfg, int timeout);

int rpma_conn_cfg_set_op_timeout(struct rpma_conn_cfg *cfg, int timeout);

int rpma_conn_cfg_delete(struct rpma_conn_cfg **cfg);


/* establishing a connection */
struct rpma_conn;

int rpma_connect(struct rpma_peer *peer, struct rpma_conn_cfg *ccfg,
		const char *addr, const char *service, struct rpma_conn **conn);

int rpma_conn_set_app_context(struct rpma_conn *conn, void *data);

int rpma_conn_get_app_context(struct rpma_conn *conn, void **data);

#define RPMA_DISCONNECT_NOW			(0)
#define RPMA_DISCONNECT_WHEN_DONE	(1 << 0)

int rpma_disconnect(struct rpma_conn **conn, int flags);


/* accepting connections */
struct rpma_socket;

int rpma_listen(struct rpma_peer *peer, struct rpma_conn_cfg *ccfg,
		const char *addr, const char *service, struct rpma_socket **socket);

int rpma_socket_fd(struct rpma_socket *socket);

int rpma_accept(struct rpma_socket *socket, struct rpma_conn **conn);

int rpma_close(struct rpma_socket **socket);


/* completion handling */
#define RPMA_OP_RECV			(1 << 0)
#define RPMA_OP_READ			(1 << 1)
#define RPMA_OP_WRITE			(1 << 2)
#define RPMA_OP_FLUSH			(RPMA_OP_READ)
/* XXX how to differentiate between READ as READ and READ as FLUSHES? */

int rpma_wait(struct rpma_conn *conn, void **op_context);

/* error handling */
const char *rpma_errormsg(void);

#ifdef __cplusplus
}
#endif
#endif	/* librpma/base.h */