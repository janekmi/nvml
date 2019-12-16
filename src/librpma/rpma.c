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
 * rpma.c -- entry points for librpma
 */

#include "librpma.h"
#include "rpma.h"

int
rpma_dispatcher_new(struct rpma_zone *zone, struct rpma_dispatcher **disp)
{
	return RPMA_E_NOSUPP;
}

int
rpma_dispatch(struct rpma_dispatcher *disp)
{
	return RPMA_E_NOSUPP;
}

int
rpma_dispatcher_delete(struct rpma_dispatcher **disp)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_new(struct rpma_zone *zone, struct rpma_connection **conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_accept(struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_reject(struct rpma_zone *zone)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_establish(struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_disconnect(struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_delete(struct rpma_connection **conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_set_custom_data(struct rpma_connection *conn, void *data)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_get_custom_data(struct rpma_connection *conn, void **data)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_get_zone(struct rpma_connection *conn, struct rpma_zone **zone)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_attach(struct rpma_connection *conn,
		struct rpma_dispatcher *disp)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_detach(struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_enqueue(struct rpma_connection *conn, rpma_queue_func func,
		void *arg)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_register_on_notify(struct rpma_connection *conn,
	rpma_on_transmission_notify_func func)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_register_on_recv(struct rpma_connection *conn,
		rpma_on_connection_recv_func func)
{
	return RPMA_E_NOSUPP;
}

int
rpma_sequence_new(struct rpma_sequence **sequence)
{
	return RPMA_E_NOSUPP;
}

int
rpma_sequence_add_step(struct rpma_sequence *sequence,
		rpma_queue_func func, void *arg)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_enqueue_sequence(struct rpma_connection *conn,
		struct rpma_sequence *sequence)
{
	return RPMA_E_NOSUPP;
}

int
rpma_sequence_delete(struct rpma_sequence **sequence)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_group_new(struct rpma_connection_group **group)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_group_add(struct rpma_connection_group *group,
		struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_group_remove(struct rpma_connection_group *group,
		struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_group_enqueue(struct rpma_connection_group *group,
		rpma_queue_func func, void *arg)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_group_delete(struct rpma_connection_group **group)
{
	return RPMA_E_NOSUPP;
}

int
rpma_msg_get_ptr(struct rpma_connection *conn, void **ptr)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_send(struct rpma_connection *conn, void *ptr)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_local_new(struct rpma_zone *zone, void *ptr, size_t size,
		int usage, struct rpma_memory_local **mem)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_local_get_ptr(struct rpma_memory_local *mem, void **ptr)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_local_get_size(struct rpma_memory_local *mem, size_t *size)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_local_get_id(struct rpma_memory_local *mem,
		struct rpma_memory_id *id)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_local_delete(struct rpma_memory_local **mem)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_remote_new(struct rpma_zone *zone, struct rpma_memory_id *id,
		struct rpma_memory_remote **rmem)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_remote_get_size(struct rpma_memory_remote *rmem, size_t *size)
{
	return RPMA_E_NOSUPP;
}

int
rpma_memory_remote_delete(struct rpma_memory_remote **rmem)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_read(struct rpma_connection *conn,
		struct rpma_memory_local *dst, size_t dst_off,
		struct rpma_memory_remote *src, size_t src_off, size_t length)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_write(struct rpma_connection *conn,
		struct rpma_memory_remote *dst, size_t dst_off,
		struct rpma_memory_local *src, size_t src_off, size_t length)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_atomic_write(struct rpma_connection *conn,
		struct rpma_memory_remote *dst, size_t dst_off,
		struct rpma_memory_local *src, size_t src_off, size_t length)
{
	return RPMA_E_NOSUPP;
}

int
rpma_connection_commit(struct rpma_connection *conn)
{
	return RPMA_E_NOSUPP;
}
