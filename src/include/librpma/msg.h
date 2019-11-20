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
 * librpma/msg.h -- definitions of librpma msg entry points (EXPERIMENTAL)
 *
 * This library provides low-level support for remote access to persistent
 * memory utilizing RDMA-capable RNICs.
 *
 * See librpma(7) for details.
 */

#ifndef LIBRPMA_MSG_H
#define LIBRPMA_MSG_H 1

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include <librpma/base.h>

#define RPMA_MSG_SEND	(1 << 0)
#define RPMA_MSG_RECV	(1 << 1)

struct rpma_msg;

int rpma_msg_new(struct rpma_zone *zone, int usage, struct rpma_msg **msg);

int rpma_msg_get_ptr(struct rpma_msg *msg, void **ptr);

int rpma_msg_delete(struct rpma_msg **msg);

int rpma_connection_send(struct rpma_connection *conn, struct rpma_msg *send);

int rpma_connection_recv_post(struct rpma_connection *conn, struct rpma_msg *recv);

int rpma_connection_recv(struct rpma_connection *conn, struct rpma_msg **recv);

#ifdef __cplusplus
}
#endif
#endif	/* librpma.h */
