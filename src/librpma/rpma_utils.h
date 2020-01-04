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
 * rpma_utils.h -- librpma utilities functions
 */

#ifndef RPMA_UTILS_H
#define RPMA_UTILS_H 1

#include <errno.h>
#include <rdma/rdma_cma.h>

#include "out.h"

#define RPMA_E_ERRNO (-errno)
#define RPMA_E_FI_ERRNO (errno)

#define ERR_STR(e, fmt, args...)\
	ERR(fmt ": %s", ## args, strerror(abs((int)(e))))

#define RPMA_CM_EVENT_TYPE_INVALID UINT32_MAX
#define RPMA_FD_INVALID (-1)

#define RPMA_DEFAULT_TIMEOUT 1000

#define RPMA_FLAG_ON(set, flag) (set) |= (flag)
#define RPMA_FLAG_OFF(set, flag) (set) &= ~(flag)

#define CQ_SIZE 10 /* XXX */

struct ec_poll {
	struct rdma_event_channel *rdma_ec;
	int epoll;
};

int rpma_utils_ec_poll_new(struct ec_poll *ec);
int rpma_utils_ec_poll_delete(struct ec_poll *ec);
int rpma_utils_ec_poll_wait(struct ec_poll *ec, int timeout);

#endif /* RPMA_UTILS_H */
