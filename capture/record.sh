#!/bin/bash
# Guided USB capture of DeepCreative actions. Run: ./record.sh
cd "$(dirname "$0")"
sudo modprobe usbmon
sudo -v || exit 1

steps=(
  "app_startup|In the OTHER terminal start the VM (sudo docker-compose up), open localhost:8006, log in, open DeepCreative. Stop when the app is fully loaded"
  "clock_mode|In DeepCreative, switch to the time/clock screen; check the cooler shows your local time"
  "clock_settings|Change any time options (12/24h, time sync, time zone...) if they exist, else skip"
)

for s in "${steps[@]}"; do
  name=${s%%|*}; action=${s#*|}
  echo
  echo "=== $name ==="
  read -rp "Press Enter to START recording (or type s + Enter to skip): " ans
  [[ $ans == s ]] && continue
  sudo dumpcap -i usbmon6 -w - > "$name.pcapng" &
  sleep 1
  echo ">>> RECORDING. Now: $action"
  read -rp ">>> When the cooler screen has finished changing, press Enter to STOP... "
  sudo pkill -INT -x dumpcap; wait
  echo "Saved $name.pcapng ($(du -h "$name.pcapng" | cut -f1))"
  read -rp "Any notes about this step? (Enter to skip): " note
  [[ -n $note ]] && echo "$name: $note" >> notes.txt
done
echo; echo "All done! Tell Claude the captures are ready."
