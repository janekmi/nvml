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
 * device.c -- entry points for librpma device
 */

#include <rdma/rdma_cma.h>

#include <librpma.h>

#include "alloc.h"
#include "info.h"
#include "device.h"
#include "rpma_utils.h"

static int
device_by_info(struct rdma_addrinfo *rai, struct ibv_context **device)
{
	struct rdma_cm_id *temp_id;
	int ret = rdma_create_id(NULL, &temp_id, NULL, RDMA_PS_TCP);
	if (ret)
		return RPMA_E_ERRNO;

	/* either bind or resolve the address */
	if (rai->ai_flags & RAI_PASSIVE) {
		ret = rdma_bind_addr(temp_id, rai->ai_src_addr);
		if (ret) {
			ret = RPMA_E_ERRNO;
			goto err_bind_addr;
		}
	} else {
		ret = rdma_resolve_addr(temp_id, rai->ai_src_addr, rai->ai_dst_addr,
				RPMA_DEFAULT_TIMEOUT);
		if (ret) {
			ret = RPMA_E_ERRNO;
			goto err_resolve_addr;
		}
	}

	/* obtain the device */
	*device = temp_id->verbs;

err_bind_addr:
err_resolve_addr:
	(void)rdma_destroy_id(temp_id);
	return ret;
}

static int
device_by_address(const char *addr, const char *service, int passive,
		struct ibv_context **device)
{
	struct rpma_info info;
	info.addr = addr;
	info.service = service;
	info.passive = passive;

	/* translate address */
	int ret = info_resolve(&info);
	if (ret)
		return ret;

	/* obtain a device by address */
	ret = device_by_info(info.rai, device);
	if (ret)
		goto err_device_by_info;

err_device_by_info:
	/* release translation info */
	info_free(&info);
	return ret;
}

int
rpma_device_by_src_address(const char *addr, struct rpma_device **rdev)
{
	/* looking for device */
	struct ibv_context *verbs = NULL;
	int ret = device_by_address(addr, NULL, RPMA_INFO_PASSIVE, &verbs);
	if (ret)
		return ret;

	ASSERTne(verbs, NULL);

	/* allocate a device object */
	struct rpma_device *tmp = Malloc(sizeof(*tmp));
	if (!tmp)
		return RPMA_E_ERRNO;

	tmp->verbs = verbs;
	*rdev = tmp;

	return 0;
}

int
rpma_device_by_dst_address(const char *addr, struct rpma_device **rdev)
{
	/* looking for device */
	struct ibv_context *verbs = NULL;
	int ret = device_by_address(addr, NULL, RPMA_INFO_ACTIVE, &verbs);
	if (ret)
		return ret;

	ASSERTne(verbs, NULL);

	/* allocate a device object */
	struct rpma_device *tmp = Malloc(sizeof(*tmp));
	if (!tmp)
		return RPMA_E_ERRNO;

	tmp->verbs = verbs;
	*rdev = tmp;

	return 0;
}

int
rpma_device_delete(struct rpma_device **rdev)
{
	Free(*rdev);
	*rdev = NULL;
	return 0;
}
