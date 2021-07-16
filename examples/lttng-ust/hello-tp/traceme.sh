#!/bin/bash

RED='\033[0;31m'
NC='\033[0m' # No Color

RED=`tput setaf 1`
GREEN=`tput setaf 2`
NC=`tput sgr0`


function usage()
{
    echo "Usage: "
    echo " "
    echo "`basename $0` [executable]"
    echo " "
    echo "Example: `basename $0` ./dynamic-dtrace"
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

echo "${RED}"
readelf -n $1
echo "${NC}"

echo " "

# create LTTng sesssion
lttng create `basename $1`

# enable events
lttng enable-event --kernel \
                   --userspace-probe=sdt:$1:my_provider:my_probe my_probe_cal

# start the tracing    
lttng start

# execute the application
$1

# stop the tracing
lttng stop

# show what we traced
echo " "
lttng view
echo " "

# destroy the LTTng session
lttng destroy

