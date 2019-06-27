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
 * librpma.h -- definitions of librpma entry points
 *
 * This library provides low-level support for remote access to persistent
 * memory utilizing RDMA-capable RNICs.
 *
 * See librpma(3) for details.
 */

#ifndef LIBRPMA_H
#define LIBRPMA_H 1

#include <sys/types.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct rpma_domain RPMAdomain;
typedef struct rpma_connection RPMAconn;

/* client-side */
RPMAconn *rpma_connect(const char *node, uint16_t service, unsigned *nlanes);
RPMAdomain *rpma_get_domain(RPMAconn *conn);
int rpma_close(RPMAconn *conn);

/* server-side */
RPMAdomain *rpma_listen(const char *node, uint16_t *service,
		unsigned *total_nlanes);
RPMAconn *rpma_accept(RPMAdomain *domain, unsigned *nlanes);
int rpma_shutdown(RPMAdomain *domain);

/* memory regions */
int rpma_mr_open(RPMAdomain *domain, void *buf, size_t len, unsigned mrid);
int rpma_mr_close(RPMAdomain *domain, int mrdes);
int rpma_mr_get(RPMAdomain *domain, unsigned mrid);
int rpma_conn_mr_get(RPMAconn *conn, unsigned mrid, size_t *len);

/* remote memory operations */
int rpma_write(RPMAconn *conn, int dest_mrdes, size_t dest_off, int src_mrdes,
		size_t src_off, size_t length, unsigned lane);
int rpma_atomic_write();
int rpma_read(RPMAconn *conn, int dest_mrdes, size_t dest_off, int src_mrdest,
		size_t src_off, size_t length, unsigned lane);
int rpma_flush(RPMAconn *conn, unsigned lane);

#ifdef __cplusplus
}
#endif
#endif	/* librpma.h */
