source "$(dirname "$0")/lib.sh"
clear
run "stitch crashes probe" 3
run "stitch exec probe:${CRASH:-c3} --trace" 3
run "stitch exec probe:${CRASH:-c3} --rev 0.15.2" 4
printf '%s' "$PS_PROMPT"; sleep 1
