# Typing helpers for the asciinema recordings (probe.sh, triage.sh). Expects a built tomlkit project in /work/tomlkit.
# Record: asciinema rec --cols 104 --rows 24 -i 1.2 -c "bash probe.sh" probe.cast
# Render: agg --theme github-dark --font-size 14 --idle-time-limit 1.5 --last-frame-duration 4 probe.cast ../../probe.gif
export PS_PROMPT=$'\033[1;34mtomlkit\033[0m \033[1;32m$\033[0m '
run() {
  printf '%s' "$PS_PROMPT"; sleep 0.5
  local s="$1"; for ((i=0;i<${#s};i++)); do printf '%s' "${s:i:1}"; sleep 0.035; done
  sleep 0.5; printf '\n'
  eval "${3:-$1}"
  sleep "${2:-2}"
}
cd /work/tomlkit
