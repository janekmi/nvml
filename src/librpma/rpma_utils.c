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
 * rpma_utils.c -- entry points for librpma RPMA utils
 */

#include <sys/epoll.h>
#include <fcntl.h>
#include <unistd.h>

#include <librpma.h>

#include "rpma_utils.h"

static int
fd_set_nonblock(int fd)
{
	int ret;

	ret = fcntl(fd, F_GETFL);
	if (ret < 0)
		return RPMA_E_ERRNO;

	int flags = ret | O_NONBLOCK;
	ret = fcntl(fd, F_SETFL, flags);
	if (ret < 0)
		return RPMA_E_ERRNO;

	return 0;
}

static int
epoll_new(int fd, int *ec_epoll)
{
	int ret;

	int epoll = epoll_create1(EPOLL_CLOEXEC);
	if (epoll < 0) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "epoll_create1");
		return ret;
	}

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = NULL;

	ret = epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event);
	if (ret < 0) {
		ret = RPMA_E_ERRNO;
		ERR_STR(ret, "epoll_ctl(EPOLL_CTL_ADD)");
		goto err_add;
	}

	*ec_epoll = epoll;

	return 0;

err_add:
	close(epoll);
	return ret;
}

int
rpma_utils_ec_poll_new(struct ec_poll *ec)
{
	/* create event channel */
	struct rdma_event_channel *tmp = rdma_create_event_channel();
	if (!tmp)
		return RPMA_E_ERRNO;

	int ret = fd_set_nonblock(tmp->fd);
	if (ret)
		goto err_fd_set_nonblock;

	ret = epoll_new(tmp->fd, &ec->epoll);
	if (ret)
		goto err_epoll_new;

	ec->rdma_ec = tmp;

	return 0;

err_epoll_new:
err_fd_set_nonblock:
	rdma_destroy_event_channel(tmp);
	return ret;
}

int
rpma_utils_ec_poll_delete(struct ec_poll *ec)
{
	int ret = close(ec->epoll);
	if (ret)
		return ret;

	ec->epoll = RPMA_FD_INVALID;
	rdma_destroy_event_channel(ec->rdma_ec);
	ec->rdma_ec = NULL;

	return 0;
}

#define MAX_EVENTS 2

int
rpma_utils_ec_poll_wait(struct ec_poll *ec, int timeout)
{
	struct epoll_event events[MAX_EVENTS];

	int ret = epoll_wait(ec->epoll, events, MAX_EVENTS, timeout);

	if (ret == 0)
		return -1; /* XXX */
	else if (ret < 0)
		return RPMA_E_ERRNO;

	return 0;
}
