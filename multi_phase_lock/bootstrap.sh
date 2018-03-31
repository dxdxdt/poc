#!/bin/bash
aclocal && automake --add-missing --copy && autoconf
EC=$?
if [ $EC -eq 0 ]; then
	exit 0
fi

REQ_PACKAGES="autoconf automake"
echo -e "Somethin went wrong!
Following packages are required to build this project: $REQ_PACKAGES" >& 2

exit $EC
