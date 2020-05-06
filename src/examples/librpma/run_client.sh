#!/bin/bash

export LD_LIBRARY_PATH=../../debug
#export RPMA_LOG_FILE=/dev/shm/librpma.log
#export RPMA_LOG_LEVEL=100

./hello c $*
