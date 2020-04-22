#!/usr/bin/env bash
# SPDX-License-Identifier: BSD-3-Clause
# Copyright 2016-2020, Intel Corporation

#
# install-valgrind.sh - installs valgrind for persistent memory
#

set -e

OS=$1

install_upstream_from_distro() {
  case "$OS" in
    fedora) dnf install -y valgrind ;;
    ubuntu) apt-get install -y --no-install-recommends valgrind ;;
    *) return 1 ;;
  esac
}

install_custom-pmem_from_source() {
  git clone https://github.com/pmem/valgrind.git
  cd valgrind
  # valgrind v3.15 with pmemcheck
  # 2020.04.22 Merge pull request #81 from marcinslusarz/pmem-3.15
  git checkout 09f75f69683d862f8456f75484fcdc0dc5508900
  ./autogen.sh
  ./configure
  make -j$(nproc)
  make -j$(nproc) install
  cd ..
  rm -rf valgrind
}

ARCH=$(uname -m)
case "$ARCH" in
  ppc64le) install_upstream_from_distro ;;
  *) install_custom-pmem_from_source ;;
esac
