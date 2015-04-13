#!/bin/sh
if [ -z "$QEMU_ARCH" ]
then
    echo Please define QEMU_ARCH environment.
    exit 2
fi
exec `dirname $0`/run-qemu-system-$QEMU_ARCH "$@"
