#!/bin/sh
NAME=`basename $0`
NAME=`echo $NAME | sed s/^run-//`

READCONFIG=
if [ ! -z "$1" ]
then
    if [ -f "$1" ]
    then
        READCONFIG="-readconfig $1"
        shift
    fi
fi

export LD_LIBRARY_PATH=
echo `dirname $0`/bin/$NAME $READCONFIG "$@"
exec `dirname $0`/bin/$NAME $READCONFIG "$@"
