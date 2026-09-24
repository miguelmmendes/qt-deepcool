#!/bin/bash
# Stop one motherboard fan output at a time for a few seconds so you can see which physical fan it is.
# Restores the original settings afterwards (also on Ctrl+C). Run with sudo.
set -u
HOLD=${HOLD:-10}
MAX_TEMP=70000
H=$(dirname "$(grep -l nct6799 /sys/class/hwmon/hwmon*/name)")
K=$(dirname "$(grep -l k10temp /sys/class/hwmon/hwmon*/name)")
CHANNELS=${*:-2 4 5}

declare -A ORIG_EN ORIG_PWM
for n in $CHANNELS; do ORIG_EN[$n]=$(cat $H/pwm${n}_enable); ORIG_PWM[$n]=$(cat $H/pwm$n); done

restore() {
  for n in $CHANNELS; do
    echo 1 > $H/pwm${n}_enable; echo ${ORIG_PWM[$n]} > $H/pwm$n; echo ${ORIG_EN[$n]} > $H/pwm${n}_enable
  done
  echo "Restored original fan settings."
}
trap restore EXIT INT TERM

rpm() { for n in $CHANNELS; do printf "fan%s=%-5s " $n "$(cat $H/fan${n}_input)"; done; printf "CPU=%s°C\n" $(( $(cat $K/temp1_input) / 1000 )); }

echo "Baseline: $(rpm)"
for n in $CHANNELS; do
  echo
  read -rp "Next: STOP fan output $n for ${HOLD}s. Watch your fans, press Enter to start... "
  echo 1 > $H/pwm${n}_enable; echo 0 > $H/pwm$n
  for ((s = 0; s < HOLD; s++)); do
    sleep 1; echo "  ${s}s: $(rpm)"
    if (( $(cat $K/temp1_input) > MAX_TEMP )); then echo "CPU too hot, aborting!"; exit 1; fi
  done
  echo ${ORIG_PWM[$n]} > $H/pwm$n; echo ${ORIG_EN[$n]} > $H/pwm${n}_enable
  echo "Fan output $n back to normal. Which fan stopped? (write it down)"
done
