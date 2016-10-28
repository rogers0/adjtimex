#!/bin/sh
# depends on package bc (GNU bc arbitrary precision calculator language)

NTPCONF=/etc/ntp.conf

test -f "$NTPCONF" &&
  DRIFTFILE=$(grep -r driftfile /etc/ntp.conf|sed s/^driftfile[[:space:]]//)
test -f "$DRIFTFILE" &&
  DRIFT=$(cat $DRIFTFILE)
test -n "$DRIFT" &&
  FREQ=$(echo "$DRIFT*65536+.5"|bc -l|cut -d. -f1)
test -n "$FREQ" &&
  echo $FREQ
