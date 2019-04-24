/*
 * Copyright 2014-2017, Intel Corporation
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
 * out_attach.c -- unit test for out_init_attach()
 */

#include <sys/types.h>
#include <stdarg.h>
#include <string.h>
#include "unittest.h"
#include "out.h"

static struct {
	char *ident;
	int log_prefix_level;
	int log_level;
	FILE *log_file;
} params;

static int
test(const struct test_case *tc, int argc, char *argv[])
{
	/* attach the log */
	out_init_attach(params.ident, params.log_prefix_level,
			params.log_level, params.log_file);

	/* cleanup */
	out_fini();

	return 0;
}

/*
 * test_cases -- available test cases
 */
static struct test_case test_cases[] = {
	TEST_CASE(test),
};

#define NTESTS	(sizeof(test_cases) / sizeof(test_cases[0]))

#define INVALID_VAL	(-1)
#define IS_INVALID(val) (val == INVALID_VAL)

#define NULL_STR "NULL"
#define IS_NULL(str) (strcmp((str), NULL_STR) == 0)

//#define NO (0)
//#define YES (1)

static char *log_prefix_level_str[] = {
	[LOG_PREFIX_LEVEL_COMPLETE] = "COMPLETE",
	[LOG_PREFIX_LEVEL_FUNC] = "FUNC",
	[LOG_PREFIX_LEVEL_NO] = "NO"
};

static int
parse_log_prefix_level(char *str)
{
	for (int i = 0; i < ARRAY_SIZE(log_prefix_level_str); ++i) {
		if (strcmp(log_prefix_level_str[i], str) == 0)
			return i;
	}

	UT_ERR("prefix-level: %s", str);
	return INVALID_VAL;
}

static int
parse_log_level(char *str)
{
	long val = strtol(str, NULL, 10);
	if (errno == ERANGE && (val == LONG_MIN || val == LONG_MAX)) {
		UT_ERR("log-level: %s", strerror(errno));
		return INVALID_VAL;
	} else if (errno == EINVAL && val == 0) {
		UT_ERR("log-level: %s", strerror(errno));
		return INVALID_VAL;
	}

	if (val > INT_MAX || val < INT_MIN) {
		UT_ERR("log-level: %ld > %d or %ld < %d",
				val, INT_MAX, val, INT_MIN);
		return INVALID_VAL;
	}

	return val;
}

static void
usage()
{
	UT_OUT("usage: out_attach <ident> <prefix-level> <log-level> "
			"<log-file> <log-file-mode>");
	UT_OUT("prefix-level:\tCOMPLETE | FUNC | NO");
}

int
main(int argc, char *argv[])
{
	START(argc, argv, "out_attach");

	/* run test with out attached */
	/* out_init_attach() requires 5 or 6 arguments */
	if (argc < 6) {
		usage();
		return 1;
	}

	params.ident = NULL;
	if (!IS_NULL(argv[1]))
		params.ident = strdup(argv[1]);

	params.log_prefix_level = parse_log_prefix_level(argv[2]);
	UT_ASSERT(!IS_INVALID(params.log_prefix_level));

	params.log_level = parse_log_level(argv[3]);
	UT_ASSERT(!IS_INVALID(params.log_level));

	params.log_file = NULL;
	if (!IS_NULL(argv[4])) {
		params.log_file = fopen(argv[4], argv[5]);
		UT_ASSERTne(params.log_file, NULL);
	}

	TEST_CASE_PROCESS(argc, argv, test_cases, NTESTS);

	if (params.log_file)
		fclose(params.log_file);

	DONE(NULL);
}
