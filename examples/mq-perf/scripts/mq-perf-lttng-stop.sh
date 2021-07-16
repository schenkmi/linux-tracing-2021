#!/bin/sh
if [ ! -z "$1" ]; then
  # delay stopping
  sleep "$1"
fi
lttng stop
lttng destroy
