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
 * info.c -- entry points for librpma info
 */

#include <arpa/inet.h>
#include <rdma/rdma_cma.h>

#include "info.h"
#include "rpma_utils.h"

const char *
info_dump(struct rpma_info *info)
{
	/* output buffer */
	static char output[50];

	struct sockaddr_in *addr_in;
	const char *addr;
	unsigned short port;

	if (info->rai->ai_family == AF_INET) {
		addr_in = (struct sockaddr_in *)info->rai->ai_src_addr;

		if (!addr_in->sin_port) {
			ERR("addr_in->sin_por == 0");
			return NULL;
		}

		/* decode information */
		addr = inet_ntoa(addr_in->sin_addr);
		port = htons(addr_in->sin_port);

		/* print to the output buffer */
		sprintf(output, "%s:%u", addr, port);
	} else {
		ASSERT(0);
	}

	return output;
}

int
info_resolve(struct rpma_info *info)
{
	/* prepare hints */
	struct rdma_addrinfo hints;
	memset(&hints, 0, sizeof(hints));
	if (info->passive == RPMA_INFO_PASSIVE)
		hints.ai_flags |= RAI_PASSIVE;
	hints.ai_qp_type = IBV_QPT_RC;
	hints.ai_port_space = RDMA_PS_TCP;

	/* query */
	int ret = rdma_getaddrinfo(info->addr, info->service, &hints, &info->rai);
	if (ret)
		return RPMA_E_ERRNO;

	return 0;
}

void
info_free(struct rpma_info *info)
{
	rdma_freeaddrinfo(info->rai);
	info->rai = NULL;
}
