#!/bin/sh
NAME=`basename $0`
NAME=`echo $NAME | sed s/^run-//`
export LD_LIBRARY_PATH=
exec `dirname $0`/bin/$NAME "$@"
