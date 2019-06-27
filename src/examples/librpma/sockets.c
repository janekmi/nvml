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
 * sockets.c -- auxiliary socket based transport for librpma examples
 */
#include <unistd.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <netdb.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "sockets.h"

static struct sockets_node *
sockets_common_new(enum sockets_node_type type, const char *addr, uint16_t port)
{
	struct sockets_node *sn = calloc(1, sizeof(*sn));
	if (!sn)
		return NULL;
	sn->type = type;
	if (addr) {
		/* XXX: use inet_aton instead */
		sn->addr = inet_addr(addr);
		if (sn->addr == INADDR_NONE)
			goto err;
	}
	sn->port = port;
	return sn;

err:
	free(sn);
	return NULL;
}

static int
sockets_close_one(int socket)
{
	if (!socket)
		return 0;

	if (shutdown(socket, SHUT_RDWR))
		return errno;

	if (close(socket))
		return errno;

	return 0;
}

static void
sockets_common_delete(struct sockets_node *sn)
{
	sockets_close_one(sn->rwfd);
	sockets_close_one(sn->listenfd);

	free(sn);
}

int
sockets_close(struct sockets_node *sn)
{
	sockets_common_delete(sn);
	return 0;
}

int
sockets_server_new(const char *addr, uint64_t port, struct sockets_node **sn_out)
{
	int ret = 0;

	struct sockets_node *sn = sockets_common_new(sockets_node_type_server, addr,
			port);

	sn->listenfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = INADDR_ANY;
	serv_addr.sin_port = sn->port;
	if (bind(sn->listenfd, (struct sockaddr *)&serv_addr, sizeof(serv_addr)))
	{
		ret = errno;
		goto err_bind;
	}

	if (listen(sn->listenfd, 0))
	{
		ret = errno;
		goto err_listen;
	}

	*sn_out = sn;

	return 0;

err_listen:
err_bind:
	sockets_common_delete(sn);
	return ret;
}

int
sockets_server_accept(struct sockets_node *sn)
{
	int ret = 0;
	struct sockaddr_in cli_addr;
	socklen_t clilen = sizeof(cli_addr);

	sn->rwfd = accept(sn->listenfd, (struct sockaddr *)&cli_addr, &clilen);
	if (sn->rwfd == -1)
	{
		ret = errno;
		goto err_accept;
	}

	return 0;

err_accept:
	sn->rwfd = 0;
	return ret;
}

int
sockets_client(const char *addr, uint64_t port, struct sockets_node **sn_out)
{
	int ret = 0;

	struct sockets_node *sn = sockets_common_new(sockets_node_type_server, addr,
			port);

	sn->rwfd = socket(AF_INET, SOCK_STREAM, 0);

	struct sockaddr_in serv_addr;
	memset(&serv_addr, 0, sizeof(serv_addr));
	serv_addr.sin_family = AF_INET;
	serv_addr.sin_addr.s_addr = sn->addr;
	serv_addr.sin_port = sn->port;
	if (connect(sn->rwfd, (struct sockaddr *)&serv_addr,sizeof(serv_addr)))
	{
		ret = errno;
		goto err_connect;
	}

	*sn_out = sn;

	return 0;

err_connect:
	sockets_common_delete(sn);
	return ret;
}

int
sockets_send(struct sockets_node *sn, void *buff, size_t len)
{
	/* XXX: error checking is missing */
	int nbytes = write(sn->rwfd, buff, len);
	if (nbytes < 0)
		return errno;

	return 0;
}

int
sockets_recv(struct sockets_node *sn, void *buff, size_t len)
{
	/* XXX: error checking is missing */
	int nbytes = read(sn->rwfd, buff, len);
	if (nbytes < 0)
		return errno;

	return 0;
}
