// SPDX-License-Identifier: BSD-3-Clause
/* Copyright 2020, Intel Corporation */

/*
 * pmem2_usc.c -- pmem2 USC tests
 */

#include "unittest.h"
#include "ut_pmem2_utils.h"

/*
 * test_read_usc -- read USC
 */
static int
test_read_usc(const struct test_case *tc, int argc, char *argv[])
{
	if (argc < 2)
		UT_FATAL("usage: test_read_usc <file> <usc-exp>");

	/* parse arguments */
	char *file = argv[0];
	uint64_t usc_exp = STRTOULL(argv[1], NULL, 10);

	/* open file and prepare source */
	int fd = OPEN(file, O_RDWR);
	UT_ASSERTne(fd, -1);
	struct pmem2_source *src;
	int ret = pmem2_source_from_fd(&src, fd);
	UT_PMEM2_EXPECT_RETURN(ret, 0);

	/* read USC and compare it to the expected value */
	uint64_t usc;
	ret = pmem2_source_device_usc(src, &usc);
	UT_ASSERTeq(usc, usc_exp);

	/* cleanup after the test */
	pmem2_source_delete(&src);
	CLOSE(fd);

	return 2;
}

/*
 * test_cases -- available test cases
 */
static struct test_case test_cases[] = {
	TEST_CASE(test_read_usc),
};

#define NTESTS (sizeof(test_cases) / sizeof(test_cases[0]))

int
main(int argc, char *argv[])
{
	START(argc, argv, "pmem2_usc");
	TEST_CASE_PROCESS(argc, argv, test_cases, NTESTS);
	DONE(NULL);
}
