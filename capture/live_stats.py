#!/usr/bin/env python3
"""Feed real Linux sensor values into the MYSTIQUE's built-in stats screens.

  live_stats.py [seconds_per_layout] [total_seconds] [layouts like 5:1,2:0,0:1,4:1]
"""
import glob, os, sys, time
import usb.util
from mystique_image import open_device, command
from experiment import data_packet, EP_DATA_OUT, EP_DATA_IN


def hwmon(name):
    for h in glob.glob("/sys/class/hwmon/hwmon*"):
        if open(f"{h}/name").read().strip() == name:
            return h


def read_int(path, default=0):
    try:
        return int(open(path).read())
    except (OSError, ValueError):
        return default


K10, NCT = hwmon("k10temp"), hwmon("nct6799")
# Board-specific guesses (ASRock B850M Pro-A): verify against BIOS H/W Monitor.
PUMP_FAN, CPU_FAN = "fan2", "fan4"
V33, V5, V12 = ("in3", 1.0), ("in4", 3.0), ("in1", 6.5)


class CpuUsage:
    def __init__(self):
        self.prev = self.read()

    @staticmethod
    def read():
        v = list(map(int, open("/proc/stat").readline().split()[1:]))
        return v[3] + v[4], sum(v)

    def percent(self):
        idle, total = self.read()
        d_idle, d_total = idle - self.prev[0], total - self.prev[1]
        self.prev = (idle, total)
        return 100.0 * (1 - d_idle / d_total) if d_total else 0.0


def ram_percent():
    info = dict(l.split(":", 1) for l in open("/proc/meminfo"))
    total, avail = int(info["MemTotal"].split()[0]), int(info["MemAvailable"].split()[0])
    return 100.0 * (total - avail) / total


def cpu_ghz():
    khz = max(read_int(p) for p in glob.glob("/sys/devices/system/cpu/cpu[0-9]*/cpufreq/scaling_cur_freq"))
    return khz / 1e6


def volts(sensor):
    name, scale = sensor
    return read_int(f"{NCT}/{name}_input") / 1000 * scale if NCT else 0.0


def field(value):
    """Encode a float as (uint16 integer part, 2-digit decimal part)."""
    value = max(0.0, min(value, 65535.99))
    whole = int(value)
    return whole & 0xFF, whole >> 8, int(round((value - whole) * 100)) % 100


def build_fields(cpu):
    values = [
        read_int(f"{K10}/temp1_input") / 1000 if K10 else 0,  # 0 CPU temp
        cpu.percent(),                                        # 1 CPU %
        ram_percent(),                                        # 2 RAM %
        volts(V33), volts(V5), volts(V12),                    # 3-5 voltages
        cpu_ghz(),                                            # 6 GHz
        read_int(f"{NCT}/{CPU_FAN}_input") if NCT else 0,     # 7 CPU fan RPM
        read_int(f"{NCT}/{PUMP_FAN}_input") if NCT else 0,    # 8 pump RPM
    ]
    fields = {}
    for k, v in enumerate(values):
        for j, b in enumerate(field(v)):
            fields[3 + 3 * k + j] = b
    return fields, values


def main():
    per = float(sys.argv[1]) if len(sys.argv) > 1 else 15
    total = float(sys.argv[2]) if len(sys.argv) > 2 else 120
    layouts = [tuple(map(int, s.split(":"))) for s in (sys.argv[3] if len(sys.argv) > 3 else "5:1,2:0,0:1,4:1,3:0,5:0").split(",")]
    dev, cpu = open_device(), CpuUsage()
    try:
        command(dev, 0x03, b"\x01")
        start, current = time.monotonic(), None
        while time.monotonic() - start < total:
            idx = int((time.monotonic() - start) // per) % len(layouts)
            if idx != current:
                current = idx
                main_, aux = layouts[idx]
                command(dev, 0x04, bytes([main_, 0, 0, aux]))
                print(time.strftime("%H:%M:%S"), f"layout main={main_} aux={aux}", flush=True)
            fields, values = build_fields(cpu)
            for pkt in (data_packet(0x10, {}), data_packet(0x01, fields)):
                dev.write(EP_DATA_OUT, pkt, timeout=2000)
                dev.read(EP_DATA_IN, 64, timeout=2000)
            time.sleep(1)
        print("values:", [round(v, 2) for v in values])
    finally:
        usb.util.release_interface(dev, 0)


if __name__ == "__main__":
    main()
