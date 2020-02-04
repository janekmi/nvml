# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2020, Intel Corporation
#
"""Unsafe shutdown utilities"""

import os
import sys
import shlex
import subprocess as sp
import abc

import futils
import tools


if sys.platform == 'win32':
    GET_DISK_NO_CMD = r'powershell (Get-Partition -DriveLetter(Get-Item {}:\)'\
                      '.PSDrive.Name).DiskNumber'

    GET_DIMMS_FROM_DISK_NO_CMD =\
        'powershell Get-PmemDisk | Where DiskNumber '\
        '-Eq {} | Format-Table -Property PhysicalDeviceIds -HideTableHeaders '\
        '-Autosize'

    def _get_dimms_from_disk_no(disk_no):
        """
        Get IDs of the device's underlying DIMMs using powershell cmdlets.
        The device is identified with a provided disk number.
        """
        cmd = GET_DIMMS_FROM_DISK_NO_CMD.format(disk_no)
        cmd = shlex.split(cmd)

        proc = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT,
                      universal_newlines=True)
        if proc.returncode:
            futils.Fail('The disk number {} could not be found'
                        .format(proc.stdout))

        dimms_out = proc.stdout.strip().strip(r'{}')
        if not dimms_out:
            raise futils.Fail('Disk number {} has no underlying DIMMs'
                              .format(disk_no))

        return dimms_out.split(',')


def _get_dev_from_testdir(testdir):
    if sys.platform == 'win32':
        return _get_dev_from_testdir_win(testdir)
    else:
        return _get_dev_from_testdir_linux(testdir)


def _get_dev_from_testdir_win(testdir):
    volume, _ = os.path.splitdrive(testdir)
    cmd = GET_DISK_NO_CMD.format(volume.rstrip(':'))
    cmd = shlex.split(cmd)
    proc = sp.run(cmd, stdout=sp.PIPE, stderr=sp.STDOUT,
                  universal_newlines=True)
    if proc.returncode:
        futils.Fail('The disk number {} could not be found'.format(volume))
    return proc.stdout.strip()


def _get_dev_from_testdir_linux(testdir):
    if not os.path.isdir(testdir):
        raise futils.Fail('{} is not an existing directory'
                          .format(testdir))
    try:
        out = sp.check_output(['df', testdir], stderr=sp.STDOUT,
                              universal_newlines=True)
    except sp.CalledProcessError as e:
        raise futils.Fail('df command failed: {}'.format(e.output)) from e
    else:
        # Assuming that correct 'df <testdir>' output looks as below:
        #
        # Filesystem      1K-blocks  Used Available Use% Mounted on
        # /dev/pmem0     1019003852 77856 967093676   1% /mnt/pmem0
        #
        # the device is the first word in the second line
        try:
            device = out.splitlines()[1].split()[0]
        except IndexError:
            raise futils.Fail('"df {}" output could not be parsed:\n{}'
                              .format(testdir, out))
        if not os.path.exists(device):
            raise futils.Fail('found device "{}" is not an existing file'
                              .format(device))
        return device


class USCTool(abc.ABC):
    """
    An abstract class implementing an interface necessary to serve as an
    unsafe shutdown handler tool for public UnsafeShutdown class.
    """
    @abc.abstractmethod
    def get_dev_dimms(self, dev):
        """
        Get underlying DIMMs of a provided device.
        """
        pass

    @abc.abstractmethod
    def inject_usc(dimm):
        """
        Inject unsafe shutdown into the DIMM.
        It is assumed that DIMM returned from get_dev_dimms
        may serve as a valid 'dimm' argument of this method.
        """
        pass

    @abc.abstractmethod
    def read_usc(dimm):
        """
        Read unsafe shutdown count from DIMM.
        It is assumed that DIMM returned from get_dev_dimms
        may serve as a valid 'dimm' argument of this method.
        """
        pass


class UnsafeShutdown:
    """
    Utility class specifying high-level workflow for handling unsafe shutdown.
    "tool" is a class representing specific application used for reading
    and injecting unsafe shutdown. It should be a subclass of USCTool abstract
    class.
    """
    def __init__(self, tool: USCTool = None):
        if tool:
            if not isinstance(tool, USCTool):
                raise futils.Fail('{} should be a subclass of USCTool'
                                  .format(tool))
            self.tool = tool

        # set default injecting tool if no tool provided
        else:
            if sys.platform == 'win32':
                self.tool = Ipmctl()
            else:
                self.tool = NdctlUSC()

    def read(self, testdir):
        """
        Read unsafe shutdown count of provided device, which is a sum
        of unsafe shutdown counts of all underlying DIMMs
        """
        dev = _get_dev_from_testdir(testdir)
        dimms = self.get_dev_dimms(dev)
        return sum(self.read_from_dimms(*dimms))

    def inject(self, testdir):
        """
        Inject unsafe shutdown into all DIMMs used by provided test directory
        """
        dev = _get_dev_from_testdir(testdir)
        dimms = self.get_dev_dimms(dev)
        self.inject_to_dimms(*dimms)

    def read_from_dimms(self, *dimms):
        """
        Read unsafe shutdown count values as a list
        """
        return [self.tool.read_usc(d) for d in dimms]

    def inject_to_dimms(self, *dimms):
        """
        Inject unsafe shutdown into specific DIMMs
        """
        for d in dimms:
            self.tool.inject_usc(d)

    def get_dev_dimms(self, dev):
        """
        Get the device's underlying DIMMs. For a particular tool their format
        should be valid as an argument input for all methods.
        """
        return self.tool.get_dev_dimms(dev)


class Ipmctl(USCTool):
    """ipmctl tool class"""
    def __init__(self):
        if sys.platform != 'win32':
            futils.fail('Ipmctl tool class is currently implemented only'
                        ' for Windows - for Linux use ndctl instead')

    def get_dev_dimms(self, dev):
        # add hex prefix for compliance with read_usc and inject_usc
        # methods
        dimms = ['0x{}'.format(d) for d in _get_dimms_from_disk_no(dev)]
        return dimms

    def read_usc(self, *dimms):
        usc = 0
        raw_cmd = 'ipmctl show -sensor -dimm {}'
        row_title = 'LatchedDirtyShutdownCount'
        for d in dimms:
            cmd = raw_cmd.format(d)
            try:
                out = sp.check_output(cmd.split(), universal_newlines=True,
                                      stderr=sp.STDOUT)
                usc_row = [o for o in out.splitlines() if row_title in o][0]

                # considering 'usc_row' looks like:
                # '0x0101 | LatchedDirtyShutdownCount   | 4           | Normal'
                # 'usc' is the value in the third column:
                usc += int(usc_row.split('|')[2].strip())
            except sp.CalledProcessError as e:
                raise futils.Fail('Reading unsafe shutdown count '
                                  'with ipmctl failed:\n{}'.format(e.output))
            except (IndexError, ValueError) as e:
                raise futils.Fail('Could not read dirty shutdown'
                                  'value from DIMM {}. \n{}'
                                  .format(d, e.output))

        return usc

    def inject_usc(self, *dimms):
        cmd_format = 'ipmctl set -dimm {} DirtyShutdown=1'
        for d in dimms:
            try:
                cmd = cmd_format.format(d).split()
                sp.check_output(cmd, universal_newlines=True,
                                stderr=sp.STDOUT)
            except sp.CalledProcessError as e:
                raise futils.Fail('Injecting unsafe shutdown into DIMM {}'
                                  ' failed: {}'.format(d, e.output))


class NdctlUSC(tools.Ndctl, USCTool):
    """
    Ndctl with additional unsafe shutdown handling methods
    implementing USCTool abstract class
    """
    def get_dev_dimms(self, dev):
        _, region = self._get_dev_info(dev)
        return region['mappings']

    def inject_usc(self, dimm):
        try:
            cmd = ['ndctl', 'inject-smart', '-U', dimm['dimm']]
            sp.check_output(cmd, stderr=sp.STDOUT,
                            universal_newlines=True)
        except KeyError as e:
            raise futils.Fail(e)
        except sp.CalledProcessError as e:
            raise futils.Fail(e.stdout)

    def read_usc(self, dimm):
        try:
            name = dimm['dimm']
        except KeyError as e:
            raise futils.Fail(e)

        out = self._cmd_out_to_json('list', '-HD', '-d', name)
        usc = int(out[0]['health']['shutdown_count'])

        return usc
