#!/bin/bash
# send commands to the linux test driver: ot.sh "key DOWN" "text 12" ...
for c in "$@"; do
  case "$c" in
    sleep*) sleep ${c#sleep } ;;
    *) echo "$c" > /tmp/otcmd; sleep 0.45 ;;
  esac
done
