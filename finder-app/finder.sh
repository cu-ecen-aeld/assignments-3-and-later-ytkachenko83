#!/bin/sh

filesdir="$1"
searchstr="$2"

if [ -z "$filesdir" ]
then
    echo "Required parameter 'filesdir' is blank.\n"
    exit 1
fi

if [ -z "$searchstr" ]
then
    echo "Required parameter 'searchstr' is blank.\n"
    exit 1
fi

if ! [ -d "$filesdir" ]
then
    echo "Directory '${filesdir}' is not found.\n"
    exit 1
fi

num_of_lines="$(grep -r $searchstr $filesdir | wc -l)"
## FIXME: filter by files only; exclude dirs
num_of_files="$(find $filesdir -type f| wc -l)"
echo "The number of files are ${num_of_files} and the number of matching lines are ${num_of_lines}\n"