#!/bin/bash
# Free-form DeepCreative exploration: record one capture per setting you try, describe it, and get
# the decoded cooler commands appended to a notes file automatically.
#
#   ./explore.sh [bus]     # bus = USB bus of the cooler (default: auto from lsusb)
#
# Start this BEFORE the VM if you want to catch DeepCreative's startup traffic.
cd "$(dirname "$0")"

BUS=${1:-$(lsusb | awk '/3633:0009/ {print $2 + 0; exit}')}
if [[ -z $BUS ]]; then
  echo "Cooler not found in lsusb (is it passed through already? pass the bus number: ./explore.sh 6)"
  exit 1
fi

SESSION="explore/$(date +%Y-%m-%d_%H%M)"
NOTES="$SESSION/notes.md"
mkdir -p "$SESSION"
sudo modprobe usbmon
sudo -v || exit 1

echo "# DeepCreative exploration $(date '+%Y-%m-%d %H:%M')" > "$NOTES"
echo "Recording on usbmon$BUS. Captures and notes go to $SESSION/"

n=0
while true; do
  n=$((n + 1))
  echo
  echo "================ Stage $n ================"
  read -rp "What setting/toggle will you change, and what do you expect it to do? (Enter = finish): " intent
  [[ -z $intent ]] && break
  slug=$(printf '%02d_%s' "$n" "$(echo "$intent" | tr 'A-Z' 'a-z' | tr -cs 'a-z0-9' '_' | cut -c1-40 | sed 's/_$//')")
  file="$SESSION/$slug.pcapng"

  read -rp "Press Enter to START recording... "
  sudo dumpcap -q -i "usbmon$BUS" -w - > "$file" &
  sleep 1
  echo ">>> RECORDING. Change the setting in DeepCreative now (one change per stage is best)."
  read -rp ">>> Press Enter to STOP once the cooler has reacted... "
  sudo pkill -INT -x dumpcap; wait

  read -rp "What happened on the cooler? (Enter to skip): " result

  decoded=$(python3 analysis/parse.py "$file" 2>&1 | tail -n +2)
  [[ -z $decoded ]] && decoded="(no control commands; only periodic data packets)"
  {
    echo
    echo "## Stage $n: $intent"
    echo "- Capture: \`$(basename "$file")\` ($(du -h "$file" | cut -f1))"
    echo "- Result: ${result:-(not described)}"
    echo '```'
    echo "$decoded"
    echo '```'
  } >> "$NOTES"
  echo "--- decoded commands ---"
  echo "$decoded" | head -20
done

echo
echo "Done: $((n - 1)) stage(s) recorded. Notes: capture/$NOTES"
