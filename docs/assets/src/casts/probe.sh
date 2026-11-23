source "$(dirname "$0")/lib.sh"
clear
run "sed -n 87,92p harness/stubs.py" 2.5 "sed -n 87,92p harness/stubs.py | pygmentize -l python -O style=github-dark"
run "stitch build" 1.5
run "stitch probe --secs 30" 4
printf '%s' "$PS_PROMPT"; sleep 1
