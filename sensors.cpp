#include "sensors.h"

#include <QDir>
#include <QFile>
#include <QStringList>
#include <QTextStream>

namespace Sensors {

static QString readFile(const QString &path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        return QString();
    }
    return QTextStream(&f).readAll().trimmed();
}

QString findChip(const QString &chip)
{
    static const QStringList superIoPrefixes = {"nct", "it87", "it86", "w83", "f71", "asus", "dell_smm"};

    QDir hwmonDir("/sys/class/hwmon");
    const QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &hwmon : hwmons) {
        QString dir = hwmonDir.filePath(hwmon);
        QString name = readFile(dir + "/name");
        if (chip == "auto") {
            for (const QString &prefix : superIoPrefixes) {
                if (name.startsWith(prefix)) {
                    return dir;
                }
            }
        } else if (name == chip) {
            return dir;
        }
    }
    return QString();
}

float readRaw(const QString &chipDir, const QString &input)
{
    if (chipDir.isEmpty() || input.isEmpty() || input == "none") {
        return 0.0f;
    }
    return readFile(QString("%1/%2_input").arg(chipDir, input)).toFloat();
}

float readVoltage(const QString &chipDir, const QString &spec)
{
    QString input = spec.section(':', 0, 0);
    bool ok = false;
    float scale = spec.section(':', 1, 1).toFloat(&ok);
    if (!ok) {
        scale = 1.0f;
    }
    return readRaw(chipDir, input) / 1000.0f * scale;
}

} // namespace Sensors
