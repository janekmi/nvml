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
 * configs.c -- entry points for librpma configs
 */

#include <string.h>

#include <librpma.h>

#include "configs.h"
#include "rpma_utils.h"
#include "alloc.h"

int
rpma_peer_cfg_new(struct rpma_peer_cfg **zcfg)
{
	struct rpma_peer_cfg *tmp = Malloc(sizeof(*tmp));
	if (!tmp)
		return RPMA_E_ERRNO;

	*zcfg = tmp;
	return 0;
}

int
rpma_peer_cfg_delete(struct rpma_peer_cfg **zcfg)
{
	Free(*zcfg);
	*zcfg = NULL;
	return 0;
}

#define RPMA_DEFAULT_MSG_SIZE 30
#define RPMA_DEFAULT_BUFF_NUM 10

int
rpma_conn_cfg_new(struct rpma_conn_cfg **cfg)
{
	struct rpma_conn_cfg *tmp = Malloc(sizeof(*tmp));
	if (!tmp)
		return RPMA_E_ERRNO;

	tmp->malloc = NULL;
	tmp->free = NULL;
	tmp->msg_size = RPMA_DEFAULT_MSG_SIZE;
	tmp->send_buffers_num = RPMA_DEFAULT_BUFF_NUM;
	tmp->recv_buffers_num = RPMA_DEFAULT_BUFF_NUM;
	tmp->setup_timeout = 0;
	tmp->op_timeout = 0;

	*cfg = tmp;
	return 0;
}

int
rpma_conn_cfg_set_msg_buffer_alloc_funcs(struct rpma_conn_cfg *cfg,
		rpma_malloc_func malloc_func, rpma_free_func free_func)
{
	cfg->malloc = malloc_func;
	cfg->free = free_func;
	return 0;
}

int
rpma_conn_cfg_set_max_msg_size(struct rpma_conn_cfg *cfg, size_t msg_size)
{
	cfg->msg_size = msg_size;
	return 0;
}

int
rpma_conn_cfg_set_send_buffers_num(struct rpma_conn_cfg *cfg, uint64_t buff_num)
{
	cfg->send_buffers_num = buff_num;
	return 0;
}

int
rpma_conn_cfg_set_recv_buffers_num(struct rpma_conn_cfg *cfg, uint64_t buff_num)
{
	cfg->recv_buffers_num = buff_num;
	return 0;
}

int
rpma_conn_cfg_set_setup_timeout(struct rpma_conn_cfg *cfg, int timeout)
{
	cfg->setup_timeout = timeout;
	return 0;
}

int
rpma_conn_cfg_set_op_timeout(struct rpma_conn_cfg *cfg, int timeout)
{
	cfg->op_timeout = timeout;
	return 0;
}

int
rpma_conn_cfg_delete(struct rpma_conn_cfg **cfg)
{
	Free(*cfg);
	*cfg = NULL;
	return 0;
}
