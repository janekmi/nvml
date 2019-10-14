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
 * pmem2.h -- internal definitions for libpmem2
 */
#ifndef PMEM2_H
#define PMEM2_H

#include <stdlib.h>

#include "libpmem2.h"

#ifdef __cplusplus
extern "C" {
#endif

#define PMEM2_MAJOR_VERSION 0
#define PMEM2_MINOR_VERSION 0

#define PMEM2_LOG_PREFIX "libpmem2"
#define PMEM2_LOG_LEVEL_VAR "PMEM2_LOG_LEVEL"
#define PMEM2_LOG_FILE_VAR "PMEM2_LOG_FILE"

struct pmem2_config {
	/* a source file descriptor / handle for the designed mapping */
#ifdef _WIN32
	HANDLE handle;
#else
	int fd;
#endif

	/* offset from the beginning of the file to the designed mapping */
	size_t offset;
	size_t length; /* length of the designed mapping */
};

struct pmem2_map {
	struct pmem2_config *cfg;
	void *addr;
	int sync;
};

#ifdef __cplusplus
}
#endif

#endif
