#!/bin/bash

RED=`tput setaf 1`
GREEN=`tput setaf 2`
NC=`tput sgr0`


function usage()
{
    echo "Usage: "
    echo " "
    echo "`basename $0` [executable]"
    echo " "
    echo "Example: `basename $0` ./pre-build-helpers"
}

function execute_with_log()
{
  echo "executing: ${RED}$1${NC}"
  $1
}

if [ -z "$1" ]; then
  usage
  exit 1
fi

echo " "

# create LTTng sesssion
lttng create `basename $1`

# enable events
lttng enable-event -u -a

# add context
lttng add-context -u -t vpid -t vtid -t procname

# start the tracing    
lttng start

# execute the application with preloading liblttng-ust-cyg-profile.so
LD_PRELOAD=liblttng-ust-libc-wrapper.so:liblttng-ust-pthread-wrapper.so:liblttng-ust-dl.so:liblttng-ust-cyg-profile.so $1

# stop the tracing
lttng stop

# show what we traced
echo " "
lttng view
echo " "

# destroy the LTTng session
lttng destroy
