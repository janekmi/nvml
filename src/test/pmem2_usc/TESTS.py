#!../env.py
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2020, Intel Corporation
#


import testframework as t
from testframework import granularity as g


@g.require_granularity(g.CACHELINE)
class Pmem2Usc(t.Test):
    test_type = t.Short
    usc_tool = t.UnsafeShutdown()

    def run(self, ctx):
        filepath = ctx.create_holey_file(16 * t.MiB, 'testfile')
        usc_exp = self.usc_tool.read(ctx.testdir)
        ctx.exec('pmem2_usc', self.test_case, filepath, usc_exp)


class TEST0(Pmem2Usc):
    """read USC"""
    test_case = "test_read_usc"
