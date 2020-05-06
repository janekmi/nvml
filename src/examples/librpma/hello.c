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
 * hello.c -- hello world for librpma
 */
#include <assert.h>
#include <errno.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <fcntl.h>
#include <pthread.h>
#include <sys/epoll.h>

#include <libpmem.h>
#include <librpma.h>

enum lang_t {en, es};

static const char *hello_str[] = {
	[en] = "Hello world!",
	[es] = "¡Hola Mundo!"
};

#define LANG_NUM	(sizeof(hello_str) / sizeof(hello_str[0]))

#define STR_SIZE	100

struct hello_t {
	enum lang_t lang;
	char str[STR_SIZE];
	uint64_t valid;
};

#define HELLO_SIZE	(sizeof(struct hello_t))

#define TYPE_SERVER ('s')
#define TYPE_CLIENT ('c')

struct base_t {
	const char *addr;
	const char *service;
	const char *file; /* applicable to server only */
	char type;
	void *specific;

	struct rpma_device *rdev;
	struct rpma_peer *peer;
	struct rpma_conn *conn;
};

struct server_t {
	struct hello_t *ptr;
	size_t total_size;

	struct rpma_socket *socket;

	struct rpma_memory *mem;
	struct rpma_memory_id id;
};

struct msg_t {
	struct rpma_memory_id id;
	uint64_t init_required;
};

struct client_t {
	struct {
		struct hello_t *ptr;
		struct rpma_memory *mem;
	} local;

	struct {
		struct rpma_memory_remote *mem;
		uint64_t init_required;
	} remote;
};

#define MAX_EVENTS 2

static int
epoll_wait_do(int epoll, int timeout)
{
	struct epoll_event events[MAX_EVENTS];

	int ret = epoll_wait(epoll, events, MAX_EVENTS, timeout);
	if (ret == 0)
		return -1;
	else if (ret < 0) {
		assert(0); /* XXX error */
		return -1;
	}

	return 0;
}

static inline void
hello_write(struct hello_t *hello, enum lang_t lang)
{
	hello->lang = lang;
	strncpy(hello->str, hello_str[hello->lang], STR_SIZE);
	hello->valid = 1;
}

static int
hello_init(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;

	hello_write(clnt->local.ptr, en);

	printf("write message to the target...\n");
	rpma_write(b->conn, clnt->remote.mem, 0, clnt->local.mem, 0,
			HELLO_SIZE, NULL /* op context */, RPMA_NON_WAITABLE);
	rpma_flush(b->conn, NULL, RPMA_WAITABLE);
	int ret = rpma_wait(b->conn, NULL);
	assert(ret == RPMA_OP_FLUSH);

	return 0;
}

static int
hello_revisit(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;
	int ret = 0;

	printf("read message from the target...\n");

	/* 1. perform a read */
	rpma_read(b->conn, clnt->local.mem, 0, clnt->remote.mem, 0,
			HELLO_SIZE, NULL, RPMA_WAITABLE);

	/* 2. wait for the read to complete */
	ret = rpma_wait(b->conn, NULL);

	assert(ret == RPMA_OP_READ);

	printf("translating...\n");
	struct hello_t *hello = clnt->local.ptr;
	enum lang_t lang = (enum lang_t)((hello->lang + 1) % LANG_NUM);
	hello_write(hello, lang);
	printf("%s\n", hello->str);

	printf("write message to the target...\n");

	/* 1. perform a write */
	rpma_write(b->conn, clnt->remote.mem, 0, clnt->local.mem, 0,
			HELLO_SIZE, NULL, RPMA_NON_WAITABLE);

	/* 2. make writes persistent on the remote peer (ordering) */
	rpma_flush(b->conn, NULL, RPMA_WAITABLE);

	/* 3. wait for the commit to complete */
	ret = rpma_wait(b->conn, NULL);
	assert(ret == RPMA_OP_FLUSH);

	return 0;
}

static int
msg_recv(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;

	struct msg_t *msg = NULL;
	size_t msg_size = 0;
	struct rpma_memory_id id;
	int ret = 0;

	/* 1. wait for a RECV to happen */
	ret = rpma_wait(b->conn, NULL);
	assert(ret == RPMA_OP_RECV);

	/* 2. get the buffer with the received message */
	rpma_recv_buffer_get(b->conn, &msg_size, (void **)&msg);
	assert(msg_size == sizeof(*msg));

	/* 3. read the data from the buffer */
	memcpy(&id, &msg->id, sizeof(id));
	clnt->remote.init_required = msg->init_required;

	/* 4. return the buffer to the library */
	rpma_recv_buffer_return(b->conn, (void **)&msg);

	/* create remote memory */
	rpma_memory_remote_new(b->peer, &id, &clnt->remote.mem);

	return 0;
}

static int
msg_send(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct server_t *svr = b->specific;

	struct msg_t *msg;

	/* 1. obtain a send buffer */
	rpma_send_buffer_get(b->conn, sizeof(*msg), (void **)&msg);

	/* 2. fill out the send buffer with the data */
	memcpy(&msg->id, &svr->id, sizeof(svr->id));
	if (!svr->ptr->valid)
		msg->init_required = 1;

	/* 3. send the message */
	rpma_send(b->conn, msg);

	return 0;
}

#define TIMEOUT_TIME (15000) /* [msec] == 15s */

#define TIMEOUT_COUNT_MAX 4

static int
common_peer_init(struct base_t *b)
{
	/* 1. prepare a peer configuration object */
	struct rpma_peer_cfg *zcfg = NULL;

	/* 2. create a peer */
	int ret = rpma_peer_new(zcfg, b->rdev, &b->peer);

	if (!b->peer) {
		fprintf(stderr, "Cannot create an RPMA peer: %d\n", ret);
		return ret;
	}

	return 0;
}

static int
common_conn_cfg_init(struct rpma_conn_cfg **ccfg)
{
	/* 1. prepare a connection configuration object */
	struct rpma_conn_cfg *tmp;
	rpma_conn_cfg_new(&tmp);
	rpma_conn_cfg_set_msg_buffer_alloc_funcs(tmp, malloc, free);
	rpma_conn_cfg_set_send_buffers_num(tmp, 10);
	rpma_conn_cfg_set_recv_buffers_num(tmp, 10);
	rpma_conn_cfg_set_max_msg_size(tmp, sizeof(struct msg_t));
	rpma_conn_cfg_set_setup_timeout(tmp, TIMEOUT_TIME);
	rpma_conn_cfg_set_op_timeout(tmp, TIMEOUT_TIME);

	*ccfg = tmp;

	return 0;
}

static int
server_init(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct server_t *svr = b->specific;

	/* 1. Finding a device by IP address */
	int ret = rpma_device_by_src_address(b->addr, &b->rdev);

	assert(ret == 0);

	/* prepare RPMA peer */
	ret = common_peer_init(b);
	assert(ret == 0);

	/* 1. register local memory for later use */
	ret = rpma_memory_new(b->peer, svr->ptr, HELLO_SIZE,
			RPMA_MR_WRITE_DST | RPMA_MR_READ_SRC, &svr->mem);
	assert(ret == 0);

	rpma_memory_get_id(svr->mem, &svr->id);

	/* prepare connection config */
	struct rpma_conn_cfg *ccfg;
	ret = common_conn_cfg_init(&ccfg);
	assert(ret == 0);

	ret = rpma_listen(b->peer, ccfg, b->addr, b->service, &svr->socket);
	assert(ret == 0);

	return 0;
}

static int
client_init(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);
	struct client_t *clnt = b->specific;

	/* obtain a device */
	int ret = rpma_device_by_dst_address(b->addr, &b->rdev);
	assert(ret == 0);

	/* prepare RPMA peer */
	ret = common_peer_init(b);
	assert(ret == 0);

	/* register local memory */
	ret = rpma_memory_new(b->peer, clnt->local.ptr, HELLO_SIZE,
			RPMA_MR_WRITE_SRC | RPMA_MR_READ_DST, &clnt->local.mem);
	assert(ret == 0);

	/* connect */
	struct rpma_conn_cfg *ccfg;
	ret = common_conn_cfg_init(&ccfg);
	assert(ret == 0);

	/* 2. establish a connection */
	ret = rpma_connect(b->peer, ccfg, b->addr, b->service, &b->conn);
	if (ret)
		goto err_connect;

	return 0;

err_connect:
	(void)rpma_conn_cfg_delete(&ccfg);
	(void)rpma_memory_delete(&clnt->local.mem);
	(void)rpma_peer_delete(&b->peer);
	(void)rpma_device_delete(&b->rdev);
	return ret;
}

/* XXX requires cleanup */
static void
peer_fini(struct base_t *b)
{
	struct client_t *clnt;
	struct server_t *svr;

	switch (b->type) {
	case TYPE_CLIENT:
		clnt = b->specific;

		rpma_memory_delete(&clnt->local.mem);
		break;
	case TYPE_SERVER:
		svr = b->specific;
		rpma_memory_delete(&svr->mem);
	}

	rpma_peer_delete(&b->peer);

	rpma_device_delete(&b->rdev);
}

static void
parse_args(int argc, char *argv[], struct base_t *b)
{
	if (argc < 4)
		goto err_usage;

	b->type = argv[1][0];
	b->addr = argv[2];
	b->service = argv[3];

	switch (b->type) {
	case TYPE_CLIENT:
		break;
	case TYPE_SERVER:
		if (argc < 5)
			goto err_usage;

		b->file = argv[4];
		break;
	default:
		goto err_usage;
	}

	return;

err_usage:
	fprintf(stderr,
			"usage:\n"
			"\t%s c <addr> <service>\n"
			"\t%s s <addr> <service> <file>\n",
			argv[0], argv[0]);
	exit(1);
}

static void *
alloc_memory()
{
	long pagesize = sysconf(_SC_PAGESIZE);
	if (pagesize < 0) {
		perror("sysconf");
		exit(1);
	}

	/* allocate a page size aligned local memory pool */
	void *mem;
	int ret = posix_memalign(&mem, pagesize, HELLO_SIZE);
	if (ret) {
		fprintf(stderr, "posix_memalign: %s\n", strerror(ret));
		exit(1);
	}

	return mem;
}

/* XXX redo in pmem2-style */
static void
server_pmem(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);
	struct server_t *svr = b->specific;

	/* try creating a memory pool */
	size_t len = 0; /* required for Device DAX */
	int flags = PMEM_FILE_CREATE; /* does not matter */
	mode_t mode = 0666;
	svr->ptr = pmem_map_file(b->file, len, flags, mode,
				&svr->total_size, NULL);

	if (!svr->ptr) {
		assert(errno == EEXIST);

		/* try opening a memory pool */
		len = 0;
		flags = 0;
		svr->ptr = pmem_map_file(b->file, len, flags, 0,
				&svr->total_size, NULL);
	}

	assert(svr->ptr != NULL);
	assert(svr->total_size >= HELLO_SIZE);
}

static int
epoll_init(int fd)
{
	int epoll = epoll_create1(EPOLL_CLOEXEC);
	if (epoll < 0)
		return epoll;

	struct epoll_event event;
	event.events = EPOLLIN;
	event.data.ptr = NULL;

	int ret = epoll_ctl(epoll, EPOLL_CTL_ADD, fd, &event);
	if (ret < 0)
		return ret;

	return epoll;
}

static int
server_main(struct base_t *b)
{
	assert(b->type == TYPE_SERVER);

	/* initialize server's memory */
	struct server_t *svr = calloc(1, sizeof(*svr));
	b->specific = svr;
	server_pmem(b);

	int ret = server_init(b);
	assert(ret == 0);

	int timeout_count = 0;
	int fd = rpma_socket_fd(svr->socket);
	int epoll = epoll_init(fd);

	do {
		/* wait for fd */
		ret = epoll_wait_do(epoll, TIMEOUT_TIME);
		if (ret) {
			printf("RPMA zone connection timeout %d\n", timeout_count);
			if (timeout_count == TIMEOUT_COUNT_MAX)
				break;

			++timeout_count;
			continue;
		}

		/* 2. accept a connection */
		ret = rpma_accept(svr->socket, &b->conn);
		assert(ret == 0);

		ret = msg_send(b);
		assert(ret == 0);

		ret = rpma_disconnect(&b->conn, RPMA_DISCONNECT_WHEN_DONE);
		assert(ret == 0);
	} while (1);

	close(epoll);

	peer_fini(b);
	pmem_unmap(svr->ptr, svr->total_size);
	free(svr);
	return ret;
}

static int
client_main(struct base_t *b)
{
	assert(b->type == TYPE_CLIENT);

	/* initialize client's memory */
	struct client_t *clnt = calloc(1, sizeof(*clnt));
	clnt->local.ptr = alloc_memory();
	b->specific = clnt;

	/* connect */
	int ret = client_init(b);
	if (ret)
		goto err_client_init;

	/* receive a message from the server */
	ret = msg_recv(b);
	assert(ret == 0);

	/* hello RPMA */
	if (clnt->remote.init_required)
		hello_init(b);
	else
		hello_revisit(b);

	/* disconnect */
	ret = rpma_disconnect(&b->conn, RPMA_DISCONNECT_NOW);
	assert(ret == 0);

	peer_fini(b);
err_client_init:
	free(clnt->local.ptr);
	free(clnt);
	return ret;
}

int
main(int argc, char *argv[])
{
	int ret = 0;

	struct base_t base;
	memset(&base, 0, sizeof(base));

	parse_args(argc, argv, &base);

	if (base.type == TYPE_CLIENT)
		ret = client_main(&base);
	else /* TYPE_SERVER */
		ret = server_main(&base);

	if (ret) {
		ret = abs(ret);
		fprintf(stderr, "hello: %s\n", strerror(ret));
	}

	return ret;
}
