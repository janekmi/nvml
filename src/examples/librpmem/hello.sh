#!/bin/bash

target=192.168.0.2

path=$(pwd)
export LD_LIBRARY_PATH=$path/../../nondebug:$LD_LIBRARY_PATH
export RPMEM_CMD="LD_LIBRARY_PATH=$LD_LIBRARY_PATH $path/../../tools/rpmemd/rpmemd --poolset-dir=$path"

./hello $target pool.set $1
