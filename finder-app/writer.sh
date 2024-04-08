#!/bin/sh

writefile="$1"
writestr="$2"

if [ -z "$writefile" ]
then
    echo "Required parameter 'writefile' is blank.\n"
    exit 1
fi

if [ -z "$writestr" ]
then
    echo "Required parameter 'writestr' is blank.\n"
    exit 1
fi

mkdir -p "$(dirname "$writefile")"
echo $writestr > $writefile