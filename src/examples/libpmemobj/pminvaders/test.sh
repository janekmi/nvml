#!/bin/bash

path=~/git/nvml/src
pmempool=$path/tools/pmempool/pmempool

poolset=initiator.poolset
poolset2=initiator2.poolset
dax=/dev/dax0.0
dax2=/dev/dax1.0
target=~/poolset-dir/target.poolset

export LD_LIBRARY_PATH=$path/nondebug
# export RPMEM_LOG_LEVEL=100
# export RPMEM_LOG_FILE=/dev/shm/librpmem.log
# export RPMEM_ENABLE_SOCKETS=1
# rdebug="gdbserver localhost:2345"
export RPMEM_CMD="LD_LIBRARY_PATH=$path/nondebug $rdebug $path/tools/rpmemd/rpmemd"
# export PMEMOBJ_LOG_LEVEL=100
# export PMEMOBJ_LOG_FILE=/dev/shm/libpmemobj.log

usage()
{
	echo "usage: $0 [command] [debug]"
	echo
	echo "Available commands are:"
	echo -e "\t- reset\t\t - clean both pools"
	echo -e "\t- obj_create\t - test basic obj pool"
	echo -e "\t- invaders\t - run the game on station01"
	echo -e "\t- seti\t\t - run the snopper"
	echo -e "\t- reload\t - replicate the game state to station02"
	echo -e "\t- invaders2\t - run the game on station02"
	echo -e "\t- restore\t - replicate the game state to station01"
}

if [ "$#" -lt "1" ]; then
	usage
	exit 1
fi

if [ "x$2" == "xdebug" ]; then
	debug="gdbserver localhost:2345"
fi

case "$1" in
reset)
	# clean both pools
	$pmempool rm $poolset
	$pmempool rm $poolset2
	;;
obj_create)
	# test basic obj pool
	$pmempool create obj $poolset
	;;
invaders)
	# run the game on station01
	$debug ./pminvaders2 $poolset
	;;
seti)
	# run the snopper
	$debug ./pmseti2 $target
	;;
reload)
	# replicate the game state to station02
	daxio -z -o $dax2
	$pmempool sync $poolset2
	;;
invaders2)
	# run the game on station02
	$debug ./pminvaders2 $poolset2
	;;
restore)
	# replicate the game state to station01
	daxio -z -o $dax
	$pmempool sync $poolset
	;;
*)
	echo "unknown command: $1"
	echo
	usage
	exit 1
esac
