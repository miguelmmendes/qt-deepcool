#ifndef SENSORS_H
#define SENSORS_H

#include <QString>

// Motherboard fan / voltage readings from a Super I/O hwmon chip (nct67xx, it87, ...).
namespace Sensors {

// hwmon directory of the first chip whose name matches `chip`
// ("auto" = first known Super I/O chip). Empty if none found.
QString findChip(const QString &chip = "auto");

// Value of <chipDir>/<input>_input (e.g. "fan2" -> RPM). 0 if missing or input is empty/"none".
float readRaw(const QString &chipDir, const QString &input);

// Voltage from a spec like "in4:3" (input in4, millivolts x 3 divider). 0 if unavailable.
float readVoltage(const QString &chipDir, const QString &spec);

} // namespace Sensors

#endif // SENSORS_H
