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
 * pmem2_windows.c -- pmem2 entry points for libpmem2 (Windows)
 */

#include "libpmem2.h"
#include "out.h"
#include "pmem2.h"
#include "pmem2_utils.h"

#define HIDWORD(x) ((DWORD)((x) >> 32))
#define LODWORD(x) ((DWORD)((x) & 0xFFFFFFFF))

/*
 * create_mapping -- creates file mapping object for a file
 */
HANDLE
create_mapping(const struct pmem2_config *cfg, DWORD protect, int *err)
{
	size_t max_size = cfg->length + cfg->offset;
	*err = 0;
	HANDLE mh = CreateFileMapping(cfg->handle,
		NULL, /* security attributes */
		protect,
		HIDWORD(max_size),
		LODWORD(max_size),
		NULL);

	*err = GetLastError();
	if (!mh || *err == ERROR_ALREADY_EXISTS)
		ERR("CreateFileMapping, error: 0x%08x", err);

	return mh;
}

/*
 * pmem2_map -- map memory according to provided config
 */
int
pmem2_map(const struct pmem2_config *cfg, struct pmem2_map **mapp)
{
	int ret = PMEM2_E_OK;
	int err = 0;

	DWORD access = FILE_MAP_ALL_ACCESS;
	HANDLE mh = create_mapping(cfg, PAGE_READWRITE, &err);
	if (ret == ERROR_ACCESS_DENIED) {
		mh = create_mapping(cfg, PAGE_READONLY, &err);
		access = FILE_MAP_READ;
	}

	if (!mh) {
		ret = PMEM2_E_EXTERNAL;
		return ret;
	} else if (err == ERROR_ALREADY_EXISTS) {
		ret = PMEM2_E_MAPPING_EXISTS;
		goto err_close_mapping_handle;
	}

	void *base = MapViewOfFileEx(mh,
		access,
		HIDWORD(cfg->offset),
		LODWORD(cfg->offset),
		cfg->length,
		NULL); /* hint address */

	if (base == NULL) {
		ERR("MapViewOfFileEx, error: 0x%08x", GetLastError());
		ret = PMEM2_E_MAP_FAILED;
		goto err_close_mapping_handle;
	}

	if (!CloseHandle(mh)) {
		ERR("MapViewOfFileEx, error: 0x%08x", GetLastError());
		ret = PMEM2_E_EXTERNAL;
		mh = NULL;
		goto err_free_base;
	}

	/* prepare pmem2_map structure */
	struct pmem2_map *map;
	map = (struct pmem2_map *)pmem2_zalloc(sizeof(*map), &ret);
	if (!map)
		goto err_free_base;
	map->cfg = (struct pmem2_config *)pmem2_zalloc(sizeof(*map->cfg), &ret);
	if (!map->cfg)
		goto err_free_map;

	memcpy(map->cfg, cfg, sizeof(*cfg));
	map->addr = NULL;

	HANDLE ph = GetCurrentProcess();
	BOOL succeeded = DuplicateHandle(ph,
		cfg->handle,
		ph,
		&map->cfg->handle,
		0,
		FALSE,
		DUPLICATE_SAME_ACCESS);

	if (!succeeded) {
		ERR("DuplicateHandle, error: 0x%08x", GetLastError());
		ret = PMEM2_E_DUP_FAILED;
		goto err_free_map;
	}

	*mapp = map;

	return ret;

err_free_map:
	free(map);
err_free_base:
	UnmapViewOfFile(&base);
err_close_mapping_handle:
	if (mh)
		CloseHandle(mh);

	return ret;
}
