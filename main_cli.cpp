/**
 * DeepCool Digital - CLI Version
 *
 * A headless command-line application for controlling DeepCool digital
 * cooling devices on Linux servers without a GUI.
 *
 * Usage: deepcool-cli [options]
 *
 * Options:
 *   -l, --list           List available devices and exit
 *   -d, --device <path>  Device path (e.g., /dev/hidraw0) or index (0, 1, ...)
 *   -i, --interval <ms>  Update interval in milliseconds (default: 1000)
 *   -m, --mode <mode>    Display mode: cpu, gpu, gpu_focus (default: cpu)
 *   -L, --layout <list>  Built-in screen(s): cpu-temp, cpu-freq, pump, cpu-fan, fans, clock
 *                        (comma-separated list rotates, see --cycle; default: cpu-temp)
 *   -a, --aux <area>     Bottom area: system (GHz/CPU/RAM), core (temp/GHz) or voltages (default: system)
 *   -c, --cycle <sec>    Seconds per screen when --layout lists several (default: 10)
 *   --pump-fan, --cpu-fan, --volt-3v3, --volt-5v, --volt-12v, --sensor-chip
 *                        Motherboard sensor mapping (see --help)
 *   -f, --fahrenheit     Use Fahrenheit instead of Celsius
 *   -V, --verbose        Enable verbose output
 *   -D, --daemon         Run as daemon (fork to background)
 *   -h, --help           Show help message
 */

#include <QCoreApplication>
#include <QCommandLineParser>
#include <QCommandLineOption>
#include <QTimer>
#include <QFile>
#include <QTextStream>
#include <QRegularExpression>
#include <QDir>
#include <QDebug>
#include <QBuffer>
#include <QCryptographicHash>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QColor>
#include <QImage>
#include <QPainter>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QMap>
#include <QSaveFile>

#include <csignal>
#include <unistd.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <fstream>
#include <sstream>
#include <vector>

#include "device.h"
#include "deepcooldevice.h"
#include "sensors.h"

// Global pointers for signal handler cleanup
static DeepCoolDevice* g_device = nullptr;
static QCoreApplication* g_app = nullptr;
static bool g_verbose = false;
static bool g_running = true;

// Previous CPU stats for usage calculation
static long long prev_idle = 0;
static long long prev_total = 0;

void log(const QString& message) {
    if (g_verbose) {
        QTextStream out(stdout);
        out << "[" << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss") << "] "
            << message << Qt::endl;
    }
}

void logError(const QString& message) {
    QTextStream err(stderr);
    err << "[ERROR] " << message << Qt::endl;
}

void logInfo(const QString& message) {
    QTextStream out(stdout);
    out << message << Qt::endl;
}

void signalHandler(int signum) {
    QTextStream out(stdout);
    out << Qt::endl << "Received signal " << signum << ", shutting down..." << Qt::endl;
    g_running = false;

    if (g_device) {
        g_device->close();
    }

    if (g_app) {
        g_app->quit();
    }
}

void setupSignalHandlers() {
    struct sigaction action;
    action.sa_handler = signalHandler;
    sigemptyset(&action.sa_mask);
    action.sa_flags = 0;

    sigaction(SIGINT, &action, nullptr);
    sigaction(SIGTERM, &action, nullptr);
    sigaction(SIGHUP, &action, nullptr);
}

bool daemonize() {
    pid_t pid = fork();

    if (pid < 0) {
        logError("Failed to fork");
        return false;
    }

    if (pid > 0) {
        // Parent process - exit
        logInfo(QString("Daemon started with PID %1").arg(pid));
        _exit(0);
    }

    // Child process continues

    // Create new session
    if (setsid() < 0) {
        logError("Failed to create new session");
        return false;
    }

    // Fork again to prevent acquiring a controlling terminal
    pid = fork();
    if (pid < 0) {
        return false;
    }
    if (pid > 0) {
        _exit(0);
    }

    // Change working directory
    chdir("/");

    // Close standard file descriptors
    close(STDIN_FILENO);
    close(STDOUT_FILENO);
    close(STDERR_FILENO);

    // Redirect to /dev/null
    open("/dev/null", O_RDONLY);
    open("/dev/null", O_WRONLY);
    open("/dev/null", O_WRONLY);

    return true;
}

float getCPUTemperature(bool useFahrenheit) {
    float celsius = 0.0f;

    // First, try to find coretemp (Intel) or k10temp (AMD) in hwmon
    // This gives the actual CPU package temperature like 'sensors' command
    QDir hwmonDir("/sys/class/hwmon");
    QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& hwmon : hwmons) {
        QString namePath = QString("/sys/class/hwmon/%1/name").arg(hwmon);
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString name = QTextStream(&nameFile).readAll().trimmed();
            nameFile.close();

            // Intel CPU: coretemp, AMD CPU: k10temp
            if (name == "coretemp" || name == "k10temp") {
                // Try to find the package/Tctl temperature (usually temp1)
                // Also check for highest core temperature
                float maxTemp = 0.0f;

                // Check temp1 through temp20 (covers most CPUs)
                for (int i = 1; i <= 20; i++) {
                    QString tempPath = QString("/sys/class/hwmon/%1/temp%2_input").arg(hwmon).arg(i);
                    QFile tempFile(tempPath);
                    if (tempFile.open(QIODevice::ReadOnly)) {
                        float temp = QTextStream(&tempFile).readAll().trimmed().toFloat() / 1000.0f;
                        tempFile.close();
                        if (temp > maxTemp && temp < 150) {
                            maxTemp = temp;
                        }
                    }
                }

                if (maxTemp > 0) {
                    celsius = maxTemp;
                    break;
                }
            }
        }
    }

    // Fallback: try thermal zones if hwmon didn't work
    if (celsius <= 0) {
        QStringList thermalPaths = {
            "/sys/class/thermal/thermal_zone0/temp",
            "/sys/class/thermal/thermal_zone1/temp",
            "/sys/class/thermal/thermal_zone2/temp"
        };

        for (const QString& path : thermalPaths) {
            QFile tempFile(path);
            if (tempFile.open(QIODevice::ReadOnly)) {
                QTextStream in(&tempFile);
                QString temp = in.readAll().trimmed();
                float tempCelsius = temp.toFloat() / 1000.0f;
                tempFile.close();

                if (tempCelsius > celsius && tempCelsius < 150) {
                    celsius = tempCelsius;
                }
            }
        }
    }

    if (celsius > 0) {
        if (useFahrenheit) {
            return celsius * 9.0f / 5.0f + 32.0f;
        }
        return celsius;
    }

    return 0.0f;
}

float getCPUUsage() {
    QFile statFile("/proc/stat");
    if (!statFile.open(QIODevice::ReadOnly)) {
        return 0.0f;
    }

    QTextStream in(&statFile);
    QString line = in.readLine();
    statFile.close();

    if (!line.startsWith("cpu ")) {
        return 0.0f;
    }

    // Parse: cpu  user nice system idle iowait irq softirq steal guest guest_nice
    QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 5) {
        return 0.0f;
    }

    long long user = parts[1].toLongLong();
    long long nice = parts[2].toLongLong();
    long long system = parts[3].toLongLong();
    long long idle = parts[4].toLongLong();
    long long iowait = parts.size() > 5 ? parts[5].toLongLong() : 0;
    long long irq = parts.size() > 6 ? parts[6].toLongLong() : 0;
    long long softirq = parts.size() > 7 ? parts[7].toLongLong() : 0;
    long long steal = parts.size() > 8 ? parts[8].toLongLong() : 0;

    long long total_idle = idle + iowait;
    long long total = user + nice + system + idle + iowait + irq + softirq + steal;

    float usage = 0.0f;

    if (prev_total > 0) {
        long long diff_idle = total_idle - prev_idle;
        long long diff_total = total - prev_total;

        if (diff_total > 0) {
            usage = (1.0f - (float)diff_idle / (float)diff_total) * 100.0f;
        }
    }

    prev_idle = total_idle;
    prev_total = total;

    return usage;
}

float getGPUTemperature(bool useFahrenheit) {
    float celsius = 0.0f;

    // Try NVIDIA first (nvidia-smi)
    QFile nvidiaSmi("/usr/bin/nvidia-smi");
    if (nvidiaSmi.exists()) {
        FILE* pipe = popen("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                celsius = atof(buffer);
            }
            pclose(pipe);

            if (celsius > 0) {
                if (useFahrenheit) {
                    return celsius * 9.0f / 5.0f + 32.0f;
                }
                return celsius;
            }
        }
    }

    // Try AMD (through hwmon)
    QDir hwmonDir("/sys/class/hwmon");
    QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);

    for (const QString& hwmon : hwmons) {
        QString namePath = QString("/sys/class/hwmon/%1/name").arg(hwmon);
        QFile nameFile(namePath);
        if (nameFile.open(QIODevice::ReadOnly)) {
            QString name = QTextStream(&nameFile).readAll().trimmed();
            nameFile.close();

            if (name == "amdgpu" || name == "radeon") {
                QString tempPath = QString("/sys/class/hwmon/%1/temp1_input").arg(hwmon);
                QFile tempFile(tempPath);
                if (tempFile.open(QIODevice::ReadOnly)) {
                    celsius = QTextStream(&tempFile).readAll().trimmed().toFloat() / 1000.0f;
                    tempFile.close();

                    if (celsius > 0) {
                        if (useFahrenheit) {
                            return celsius * 9.0f / 5.0f + 32.0f;
                        }
                        return celsius;
                    }
                }
            }
        }
    }

    // Try Intel GPU
    QFile intelTemp("/sys/class/drm/card0/device/hwmon/hwmon*/temp1_input");
    // Fallback: no GPU temperature available
    return 0.0f;
}

float getGPUUsage() {
    // Try NVIDIA
    QFile nvidiaSmi("/usr/bin/nvidia-smi");
    if (nvidiaSmi.exists()) {
        FILE* pipe = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buffer[128];
            if (fgets(buffer, sizeof(buffer), pipe) != nullptr) {
                float usage = atof(buffer);
                pclose(pipe);
                if (usage >= 0) {
                    return usage;
                }
            }
            pclose(pipe);
        }
    }

    // Try AMD (through /sys)
    QFile amdUsage("/sys/class/drm/card0/device/gpu_busy_percent");
    if (amdUsage.open(QIODevice::ReadOnly)) {
        float usage = QTextStream(&amdUsage).readAll().trimmed().toFloat();
        amdUsage.close();
        return usage;
    }

    return 0.0f;
}

float getRAMUsage() {
    QFile meminfoFile("/proc/meminfo");
    if (!meminfoFile.open(QIODevice::ReadOnly)) {
        return 0.0f;
    }

    QTextStream in(&meminfoFile);
    QString content = in.readAll();
    meminfoFile.close();

    qint64 memTotal = 0, memAvailable = 0;
    QStringList lines = content.split('\n');
    QRegularExpression whitespaceRegex("\\s+");

    for (const QString& line : lines) {
        if (line.startsWith("MemTotal:")) {
            memTotal = line.split(whitespaceRegex)[1].toLongLong();
        } else if (line.startsWith("MemAvailable:")) {
            memAvailable = line.split(whitespaceRegex)[1].toLongLong();
        }
    }

    if (memTotal > 0) {
        return ((memTotal - memAvailable) / (float)memTotal) * 100.0f;
    }

    return 0.0f;
}

void listDevices() {
    logInfo("Scanning for DeepCool devices...\n");

    QVector<DeviceInfo> devices = Device::detectDevices();

    if (devices.isEmpty()) {
        logInfo("No DeepCool devices found.");
        logInfo("\nTroubleshooting:");
        logInfo("  1. Make sure the device is connected via USB");
        logInfo("  2. Run with sudo or set up udev rules");
        logInfo("  3. Check 'lsusb' for vendor ID 3633");
        return;
    }

    logInfo(QString("Found %1 device(s):\n").arg(devices.size()));

    for (int i = 0; i < devices.size(); i++) {
        const DeviceInfo& dev = devices[i];
        QString typeStr = (dev.type == DEVICE_TYPE_HID) ? "HID" : "USB";
        logInfo(QString("  [%1] %2").arg(i).arg(dev.displayName));
        logInfo(QString("      Type: %1").arg(typeStr));
        logInfo(QString("      Path: %1").arg(dev.devicePath));
        logInfo(QString("      VID:PID: %1:%2")
            .arg(dev.vendorId, 4, 16, QChar('0'))
            .arg(dev.productId, 4, 16, QChar('0')));
        if (dev.type == DEVICE_TYPE_USB_VENDOR) {
            logInfo(QString("      Bus: %1, Address: %2").arg(dev.busNumber).arg(dev.deviceAddress));
        }
        logInfo("");
    }
}

DisplayMode parseDisplayMode(const QString& mode) {
    QString m = mode.toLower();
    if (m == "cpu" || m == "0") return MODE_CPU_INFO;
    if (m == "gpu" || m == "1") return MODE_GPU_INFO;
    if (m == "gpu_focus" || m == "4") return MODE_GPU_FOCUS;
    if (m == "system" || m == "2") return MODE_SYSTEM_OVERVIEW;
    if (m == "custom" || m == "3") return MODE_CUSTOM;
    return MODE_CPU_INFO;
}

bool parseScreen(const QString& name, MainScreen& screen) {
    static const QMap<QString, MainScreen> screens = {
        {"cpu-freq", SCREEN_CPU_FREQ}, {"clock", SCREEN_CLOCK}, {"pump", SCREEN_PUMP},
        {"cpu-fan", SCREEN_CPU_FAN}, {"fans", SCREEN_FANS}, {"cpu-temp", SCREEN_CPU_TEMP},
    };
    auto it = screens.find(name.trimmed().toLower());
    if (it == screens.end()) return false;
    screen = it.value();
    return true;
}

QString screenName(MainScreen screen) {
    switch (screen) {
        case SCREEN_CPU_FREQ: return "cpu-freq";
        case SCREEN_CLOCK:    return "clock";
        case SCREEN_PUMP:     return "pump";
        case SCREEN_CPU_FAN:  return "cpu-fan";
        case SCREEN_FANS:     return "fans";
        case SCREEN_CPU_TEMP: return "cpu-temp";
    }
    return "cpu-temp";
}

static const QMap<QString, AuxArea> kAuxAreas = {
    {"voltages", AUX_VOLTAGES}, {"system", AUX_SYSTEM}, {"core", AUX_CORE},
};

static const QMap<QString, ScreenMode> kScreenModes = {
    {"stats", MODE_STATS}, {"image", MODE_IMAGE}, {"history", MODE_HISTORY},
};
static const QMap<QString, LedMode> kLedModes = {
    {"temperature", LED_TEMPERATURE}, {"motherboard", LED_MOTHERBOARD}, {"picture", LED_IMAGE_EDGE},
};
static const QMap<QString, IdleMode> kIdleModes = {
    {"off", IDLE_SCREEN_OFF}, {"animation", IDLE_ANIMATION},
};

// Desired display state. Set from CLI options, then overridden live by the control file
// (written by the Omarchy plugin):
//   {"mode": "stats"|"image"|"history", "screens": ["cpu-temp", ...], "aux": "system",
//    "cycle": 10, "image": "/path/to/picture.png", "rotation": 0, "brightness": 50,
//    "led": "temperature"|"motherboard"|"picture", "ledColor": "#ff8800", "idle": "off"|"animation"}
struct Control {
    ScreenMode screenMode = MODE_STATS;
    QList<MainScreen> screens = {SCREEN_CPU_TEMP};
    AuxArea aux = AUX_SYSTEM;
    int cycleSeconds = 10;
    QString imagePath;
    int rotationDeg = 0;
    int brightness = 50;
    LedMode led = LED_TEMPERATURE;
    QString ledColor;           // with led=picture: border colour added around the picture
    IdleMode idle = IDLE_ANIMATION;

    bool sameDisplaySettings(const Control& o) const {
        return rotationDeg == o.rotationDeg && brightness == o.brightness && led == o.led && idle == o.idle;
    }
    bool operator==(const Control& o) const {
        return screenMode == o.screenMode && screens == o.screens && aux == o.aux &&
               cycleSeconds == o.cycleSeconds && imagePath == o.imagePath && ledColor == o.ledColor &&
               sameDisplaySettings(o);
    }
};

// Merge the keys present in a control file into `control`. Unknown values are ignored.
bool loadControlFile(const QString& path, Control& control) {
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) return false;
    QJsonParseError err;
    QJsonObject obj = QJsonDocument::fromJson(f.readAll(), &err).object();
    if (err.error != QJsonParseError::NoError) {
        logError(QString("Ignoring invalid control file %1: %2").arg(path, err.errorString()));
        return false;
    }
    QString mode = obj.value("mode").toString();
    if (kScreenModes.contains(mode)) control.screenMode = kScreenModes.value(mode);
    if (obj.contains("screens")) {
        QList<MainScreen> screens;
        for (const QJsonValue& v : obj.value("screens").toArray()) {
            MainScreen s;
            if (parseScreen(v.toString(), s)) screens.append(s);
        }
        if (!screens.isEmpty()) control.screens = screens;
    }
    QString aux = obj.value("aux").toString();
    if (kAuxAreas.contains(aux)) control.aux = kAuxAreas.value(aux);
    if (obj.contains("cycle")) control.cycleSeconds = qMax(1, obj.value("cycle").toInt(10));
    if (obj.contains("image")) control.imagePath = obj.value("image").toString();
    if (obj.contains("rotation")) control.rotationDeg = obj.value("rotation").toInt(0);
    if (obj.contains("brightness")) control.brightness = qBound(0, obj.value("brightness").toInt(50), 100);
    QString led = obj.value("led").toString();
    if (kLedModes.contains(led)) control.led = kLedModes.value(led);
    if (obj.contains("ledColor")) control.ledColor = obj.value("ledColor").toString();
    QString idle = obj.value("idle").toString();
    if (kIdleModes.contains(idle)) control.idle = kIdleModes.value(idle);
    return true;
}

// Scale/crop an image to the 480x640 portrait panel and encode it like DeepCreative does.
// With a valid `borderColor`, a solid frame is painted around the edge: in "picture" LED mode
// the ring takes the colour of the picture's edge, so this sets the ring colour. Without a
// picture, the frame goes around a black screen.
QByteArray toPanelJpeg(const QString& path, const QString& borderColor = QString()) {
    QColor border(borderColor);
    QImage img;
    if (!path.isEmpty()) {
        img = QImage(path);
        if (img.isNull()) return QByteArray();
        img = img.convertToFormat(QImage::Format_RGB888)
                 .scaled(480, 640, Qt::KeepAspectRatioByExpanding, Qt::SmoothTransformation);
        img = img.copy((img.width() - 480) / 2, (img.height() - 640) / 2, 480, 640);
    } else if (border.isValid()) {
        img = QImage(480, 640, QImage::Format_RGB888);
        img.fill(Qt::black);
    } else {
        return QByteArray();
    }
    if (border.isValid()) {
        const int width = 24;
        QPainter painter(&img);
        painter.fillRect(0, 0, 480, width, border);
        painter.fillRect(0, 640 - width, 480, width, border);
        painter.fillRect(0, 0, width, 640, border);
        painter.fillRect(480 - width, 0, width, 640, border);
    }
    QByteArray jpeg;
    QBuffer buffer(&jpeg);
    buffer.open(QIODevice::WriteOnly);
    img.save(&buffer, "JPEG", 85);
    return jpeg;
}

QString defaultStatusPath() {
    QString runtime = qEnvironmentVariable("XDG_RUNTIME_DIR");
    if (runtime.isEmpty()) runtime = QString("/tmp/deepcool-%1").arg(getuid());
    return runtime + "/deepcool/status.json";
}

void writeStatus(const QString& path, const QJsonObject& status) {
    QDir().mkpath(QFileInfo(path).path());
    QSaveFile f(path);
    if (f.open(QIODevice::WriteOnly)) {
        f.write(QJsonDocument(status).toJson(QJsonDocument::Compact));
        f.commit();
    }
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    QCoreApplication::setApplicationName("deepcool-cli");
    QCoreApplication::setApplicationVersion("1.0.0");

    g_app = &app;

    // Command line parser
    QCommandLineParser parser;
    parser.setApplicationDescription("DeepCool Digital Controller - CLI Version\n"
                                     "Control DeepCool digital cooling devices from the command line.");
    parser.addHelpOption();
    parser.addVersionOption();

    // Options
    QCommandLineOption listOption(QStringList() << "l" << "list",
        "List available devices and exit");
    parser.addOption(listOption);

    QCommandLineOption deviceOption(QStringList() << "d" << "device",
        "Device path (/dev/hidrawX) or index (0, 1, ...)", "device", "0");
    parser.addOption(deviceOption);

    QCommandLineOption intervalOption(QStringList() << "i" << "interval",
        "Update interval in milliseconds", "ms", "1000");
    parser.addOption(intervalOption);

    QCommandLineOption modeOption(QStringList() << "m" << "mode",
        "Display mode: cpu, gpu, gpu_focus", "mode", "cpu");
    parser.addOption(modeOption);

    QCommandLineOption layoutOption(QStringList() << "L" << "layout",
        "Built-in screen(s): cpu-temp, cpu-freq, pump, cpu-fan, fans, clock. "
        "A comma-separated list rotates every --cycle seconds.", "screens", "cpu-temp");
    parser.addOption(layoutOption);

    QCommandLineOption auxOption(QStringList() << "a" << "aux",
        "Bottom area: system (GHz / CPU % / RAM %), core (CPU temp / GHz) or voltages (3.3 / 5 / 12 V)",
        "area", "system");
    parser.addOption(auxOption);

    QCommandLineOption cycleOption(QStringList() << "c" << "cycle",
        "Seconds per screen when --layout lists several", "seconds", "10");
    parser.addOption(cycleOption);

    QCommandLineOption imageOption("image",
        "Show this picture instead of stats (scaled/cropped to 480x640). Uploads write the "
        "cooler's flash, so it is only re-uploaded when the picture changes.", "file");
    parser.addOption(imageOption);

    QCommandLineOption brightnessOption("brightness",
        "Screen brightness 0-100 (0 turns the screen off)", "percent", "50");
    parser.addOption(brightnessOption);

    QCommandLineOption ledOption("led",
        "LED ring colour source: temperature, motherboard (ARGB sync), or picture (edge colour)",
        "mode", "temperature");
    parser.addOption(ledOption);

    QCommandLineOption controlOption("control",
        "JSON file that overrides the display settings live (used by the Omarchy plugin)",
        "file", QDir::homePath() + "/.config/deepcool/control.json");
    parser.addOption(controlOption);

    QCommandLineOption statusOption("status",
        "JSON file where live readings and state are written every update", "file", defaultStatusPath());
    parser.addOption(statusOption);

    QCommandLineOption chipOption("sensor-chip",
        "hwmon chip for fans/voltages, e.g. nct6799 ('auto' = first Super I/O chip)", "name", "auto");
    parser.addOption(chipOption);

    QCommandLineOption pumpFanOption("pump-fan",
        "hwmon fan input shown as pump speed ('none' to disable)", "input", "fan2");
    parser.addOption(pumpFanOption);

    QCommandLineOption cpuFanOption("cpu-fan",
        "hwmon fan input shown as CPU fan speed ('none' to disable)", "input", "fan4");
    parser.addOption(cpuFanOption);

    QCommandLineOption volt3v3Option("volt-3v3",
        "hwmon input and divider for the 3.3 V reading", "input:scale", "in3:1");
    parser.addOption(volt3v3Option);

    QCommandLineOption volt5vOption("volt-5v",
        "hwmon input and divider for the 5 V reading", "input:scale", "in4:3");
    parser.addOption(volt5vOption);

    QCommandLineOption volt12vOption("volt-12v",
        "hwmon input and divider for the 12 V reading", "input:scale", "in1:6.5");
    parser.addOption(volt12vOption);

    QCommandLineOption fahrenheitOption(QStringList() << "f" << "fahrenheit",
        "Use Fahrenheit instead of Celsius");
    parser.addOption(fahrenheitOption);

    QCommandLineOption verboseOption(QStringList() << "V" << "verbose",
        "Enable verbose output");
    parser.addOption(verboseOption);

    QCommandLineOption daemonOption(QStringList() << "D" << "daemon",
        "Run as daemon (fork to background)");
    parser.addOption(daemonOption);

    QCommandLineOption rotateOption(QStringList() << "r" << "rotate",
        "Screen rotation: 0, 90, 180, 270 (degrees)", "degrees", "0");
    parser.addOption(rotateOption);

    parser.process(app);

    g_verbose = parser.isSet(verboseOption);

    // Validate display options before touching the device
    Control control;
    control.screens.clear();
    for (const QString& name : parser.value(layoutOption).split(',', Qt::SkipEmptyParts)) {
        MainScreen screen;
        if (!parseScreen(name, screen)) {
            logError(QString("Unknown layout '%1'. Use: cpu-temp, cpu-freq, pump, cpu-fan, fans, clock").arg(name));
            return 1;
        }
        control.screens.append(screen);
    }
    if (control.screens.isEmpty()) {
        control.screens.append(SCREEN_CPU_TEMP);
    }
    QString auxName = parser.value(auxOption).toLower();
    if (!kAuxAreas.contains(auxName)) {
        logError(QString("Unknown aux area '%1'. Use: system, core, voltages").arg(auxName));
        return 1;
    }
    control.aux = kAuxAreas.value(auxName);
    control.cycleSeconds = qMax(1, parser.value(cycleOption).toInt());
    control.rotationDeg = parser.value(rotateOption).toInt();
    control.brightness = qBound(0, parser.value(brightnessOption).toInt(), 100);
    if (!kLedModes.contains(parser.value(ledOption))) {
        logError(QString("Unknown LED mode '%1'. Use: temperature, motherboard, picture").arg(parser.value(ledOption)));
        return 1;
    }
    control.led = kLedModes.value(parser.value(ledOption));
    if (parser.isSet(imageOption)) {
        control.screenMode = MODE_IMAGE;
        control.imagePath = parser.value(imageOption);
    }

    // The control file (if present) wins over command-line options
    const QString controlPath = parser.value(controlOption);
    const QString statusPath = parser.value(statusOption);
    QDateTime controlMtime = QFileInfo(controlPath).lastModified();
    if (loadControlFile(controlPath, control)) {
        logInfo(QString("Using settings from %1").arg(controlPath));
    }

    // List devices mode
    if (parser.isSet(listOption)) {
        listDevices();
        return 0;
    }

    // Daemon mode
    if (parser.isSet(daemonOption)) {
        if (!daemonize()) {
            logError("Failed to daemonize");
            return 1;
        }
    }

    // Setup signal handlers
    setupSignalHandlers();

    // Check for root permissions
    if (geteuid() != 0) {
        logInfo("Warning: Not running as root. Device access may fail.");
        logInfo("Run with sudo or set up udev rules for non-root access.\n");
    }

    // Detect devices
    log("Detecting devices...");
    QVector<DeviceInfo> devices = Device::detectDevices();

    if (devices.isEmpty()) {
        logError("No DeepCool devices found. Use -l to list devices.");
        return 1;
    }

    // Select device
    DeviceInfo selectedDevice;
    QString deviceArg = parser.value(deviceOption);

    bool isIndex = false;
    int deviceIndex = deviceArg.toInt(&isIndex);

    if (isIndex && deviceIndex >= 0 && deviceIndex < devices.size()) {
        selectedDevice = devices[deviceIndex];
    } else {
        // Try to find by path
        bool found = false;
        for (const DeviceInfo& dev : devices) {
            if (dev.devicePath == deviceArg) {
                selectedDevice = dev;
                found = true;
                break;
            }
        }

        if (!found) {
            // Default to first device
            selectedDevice = devices[0];
            log(QString("Device '%1' not found, using first device").arg(deviceArg));
        }
    }

    logInfo(QString("Selected device: %1").arg(selectedDevice.displayName));
    logInfo(QString("Device path: %1").arg(selectedDevice.devicePath));

    // Create and open device
    DeepCoolDevice device;
    g_device = &device;

    if (!device.open(selectedDevice)) {
        logError(QString("Failed to open device: %1").arg(selectedDevice.devicePath));
        logError("Make sure you have permission to access the device.");
        logError("Try running with sudo or setting up udev rules.");
        return 1;
    }

    logInfo("Device opened successfully.");

    // Parse options
    int interval = parser.value(intervalOption).toInt();
    if (interval < 100) interval = 100;
    if (interval > 10000) interval = 10000;

    bool useFahrenheit = parser.isSet(fahrenheitOption);
    DisplayMode displayMode = parseDisplayMode(parser.value(modeOption));

    auto toRotation = [](int deg) {
        switch (deg) {
            case 90:  return ROTATION_90;
            case 180: return ROTATION_180;
            case 270: return ROTATION_270;
            default:  return ROTATION_0;
        }
    };

    // Set before init so the temperature source is right from the first packet
    device.setDisplayMode(displayMode);

    // Always initialize the device to Machine Info mode
    logInfo("Initializing device...");
    if (device.initMachineInfoMode()) {
        logInfo("Device initialized successfully.");
    } else {
        logInfo("Warning: Device init returned false, display may not update.");
    }

    logInfo(QString("Update interval: %1 ms").arg(interval));
    logInfo(QString("Display mode: %1").arg(parser.value(modeOption)));
    logInfo(QString("Temperature unit: %1").arg(useFahrenheit ? "Fahrenheit" : "Celsius"));
    logInfo(QString("Control file: %1").arg(controlPath));
    logInfo(QString("Status file: %1").arg(statusPath));
    logInfo("");

    // Only re-upload when the encoded picture differs from what we last put in flash
    const QString uploadedHashPath = QDir::homePath() + "/.cache/deepcool/uploaded-image.md5";
    auto readFileText = [](const QString& path) {
        QFile f(path);
        return f.open(QIODevice::ReadOnly) ? QString::fromUtf8(f.readAll()).trimmed() : QString();
    };

    int screenIndex = 0;
    QElapsedTimer screenTimer, clockTimer;
    QString lastError;
    bool applied = false;
    Control current;

    // Bring the device in line with `wanted`, touching only what changed
    auto applyControl = [&](const Control& wanted) {
        lastError.clear();
        if (!applied || !wanted.sameDisplaySettings(current)) {
            if (!device.setDisplaySettings(toRotation(wanted.rotationDeg), wanted.led,
                                           wanted.brightness, wanted.idle)) {
                lastError = "Failed to apply display settings";
            }
        }
        if (wanted.screenMode == MODE_IMAGE) {
            QString border = wanted.led == LED_IMAGE_EDGE ? wanted.ledColor : QString();
            QByteArray jpeg = toPanelJpeg(wanted.imagePath, border);
            if (jpeg.isEmpty()) {
                lastError = wanted.imagePath.isEmpty() ? "No picture selected"
                                                       : "Cannot read picture " + wanted.imagePath;
            } else {
                QString hash = QCryptographicHash::hash(jpeg, QCryptographicHash::Md5).toHex();
                if (hash != readFileText(uploadedHashPath)) {
                    logInfo(QString("Uploading %1 (%2 KB)...")
                        .arg(wanted.imagePath.isEmpty() ? "colour frame" : wanted.imagePath).arg(jpeg.size() / 1024));
                    if (device.uploadImage(jpeg)) {
                        QDir().mkpath(QFileInfo(uploadedHashPath).path());
                        QSaveFile f(uploadedHashPath);
                        if (f.open(QIODevice::WriteOnly)) {
                            f.write(hash.toUtf8());
                            f.commit();
                        }
                    } else {
                        lastError = "Picture upload failed";
                    }
                } else if (!device.setScreenMode(MODE_IMAGE)) {
                    lastError = "Failed to switch to picture mode";
                }
            }
        } else if (wanted.screenMode == MODE_HISTORY) {
            if (!device.setScreenMode(MODE_HISTORY)) {
                lastError = "Failed to switch to history mode";
            }
        } else {
            if (applied && current.screenMode != MODE_STATS) {
                device.setScreenMode(MODE_STATS);
            }
            screenIndex = 0;
            if (!device.setLayout(wanted.screens.first(), wanted.aux)) {
                lastError = "Failed to set screen layout";
            }
        }
        if (!lastError.isEmpty()) {
            logError(lastError);
        }
        current = wanted;
        applied = true;
        screenTimer.restart();

        QStringList names;
        for (MainScreen sc : wanted.screens) names << screenName(sc);
        QString what = wanted.screenMode == MODE_IMAGE ? QString("picture %1").arg(wanted.imagePath)
                     : wanted.screenMode == MODE_HISTORY ? QString("history graphs")
                     : QString("%1 | aux: %2%3").arg(names.join(','), kAuxAreas.key(wanted.aux),
                           wanted.screens.size() > 1 ? QString(" | cycle: %1 s").arg(wanted.cycleSeconds) : QString());
        logInfo(QString("Showing %1 | brightness %2 | LED %3%4")
            .arg(what).arg(wanted.brightness).arg(kLedModes.key(wanted.led),
                 wanted.led == LED_IMAGE_EDGE && !wanted.ledColor.isEmpty() ? " " + wanted.ledColor : QString()));
    };

    applyControl(control);
    clockTimer.start();

    // Motherboard sensors for fan/pump RPM and voltages
    QString sensorChip = Sensors::findChip(parser.value(chipOption));
    if (sensorChip.isEmpty()) {
        logInfo("Warning: No motherboard sensor chip found; fan speeds and voltages will show 0.");
    } else {
        logInfo(QString("Sensor chip: %1 (pump=%2, cpu fan=%3)")
            .arg(sensorChip, parser.value(pumpFanOption), parser.value(cpuFanOption)));
    }

    // Initial CPU usage read (need two samples)
    getCPUUsage();

    logInfo("Starting monitoring... (Press Ctrl+C to stop)\n");

    // Setup update timer
    QTimer updateTimer;
    QObject::connect(&updateTimer, &QTimer::timeout, [&]() {
        if (!g_running) {
            app.quit();
            return;
        }

        // Gather system data
        SystemData data;
        data.cpuTemp = getCPUTemperature(useFahrenheit);
        data.cpuUsage = getCPUUsage();
        data.gpuTemp = getGPUTemperature(useFahrenheit);
        data.gpuUsage = getGPUUsage();
        data.ramUsage = getRAMUsage();
        data.useFahrenheit = useFahrenheit;
        data.pumpRpm = Sensors::readRaw(sensorChip, parser.value(pumpFanOption));
        data.cpuFanRpm = Sensors::readRaw(sensorChip, parser.value(cpuFanOption));
        data.volt3v3 = Sensors::readVoltage(sensorChip, parser.value(volt3v3Option));
        data.volt5v = Sensors::readVoltage(sensorChip, parser.value(volt5vOption));
        data.volt12v = Sensors::readVoltage(sensorChip, parser.value(volt12vOption));

        // Pick up changes from the control file (written by the Omarchy plugin)
        QDateTime mtime = QFileInfo(controlPath).lastModified();
        if (mtime.isValid() && mtime != controlMtime) {
            controlMtime = mtime;
            Control wanted = current;
            if (loadControlFile(controlPath, wanted) && !(wanted == current)) {
                applyControl(wanted);
            }
        }

        // Rotate built-in screens
        if (current.screenMode == MODE_STATS && current.screens.size() > 1 &&
            screenTimer.elapsed() >= current.cycleSeconds * 1000LL) {
            screenIndex = (screenIndex + 1) % current.screens.size();
            device.setLayout(current.screens[screenIndex], current.aux);
            screenTimer.restart();
        }

        // Keep the device clock from drifting (and follow DST changes)
        if (clockTimer.elapsed() >= 3600 * 1000LL) {
            device.syncClock();
            clockTimer.restart();
        }

        // Send to device
        bool success = device.updateDisplay(data);

        QJsonArray screenList;
        for (MainScreen sc : current.screens) screenList.append(screenName(sc));
        writeStatus(statusPath, QJsonObject{
            {"connected", success},
            {"updated", QDateTime::currentSecsSinceEpoch()},
            {"error", lastError},
            {"mode", kScreenModes.key(current.screenMode)},
            {"brightness", current.brightness},
            {"led", kLedModes.key(current.led)},
            {"ledColor", current.ledColor},
            {"idle", kIdleModes.key(current.idle)},
            {"screen", screenName(current.screens.value(screenIndex, SCREEN_CPU_TEMP))},
            {"screens", screenList},
            {"aux", kAuxAreas.key(current.aux)},
            {"cycle", current.cycleSeconds},
            {"image", current.imagePath},
            {"rotation", current.rotationDeg},
            {"cpuTemp", data.cpuTemp},
            {"cpuUsage", data.cpuUsage},
            {"ramUsage", data.ramUsage},
            {"gpuTemp", data.gpuTemp},
            {"pumpRpm", data.pumpRpm},
            {"cpuFanRpm", data.cpuFanRpm},
            {"volt3v3", data.volt3v3},
            {"volt5v", data.volt5v},
            {"volt12v", data.volt12v},
            {"unit", useFahrenheit ? "F" : "C"},
        });

        // Log status
        QString tempUnit = useFahrenheit ? "F" : "C";
        log(QString("CPU: %1%2 (%3%) | GPU: %4%5 (%6%) | RAM: %7% | pump %8 rpm | fan %9 rpm | "
                    "%10/%11/%12 V | %13")
            .arg(data.cpuTemp, 0, 'f', 1).arg(tempUnit)
            .arg(data.cpuUsage, 0, 'f', 1)
            .arg(data.gpuTemp, 0, 'f', 1).arg(tempUnit)
            .arg(data.gpuUsage, 0, 'f', 1)
            .arg(data.ramUsage, 0, 'f', 1)
            .arg(data.pumpRpm, 0, 'f', 0)
            .arg(data.cpuFanRpm, 0, 'f', 0)
            .arg(data.volt3v3, 0, 'f', 2)
            .arg(data.volt5v, 0, 'f', 2)
            .arg(data.volt12v, 0, 'f', 2)
            .arg(success ? "OK" : "FAIL"));
    });

    updateTimer.start(interval);

    // Run event loop
    int result = app.exec();

    // Cleanup
    writeStatus(statusPath, QJsonObject{{"connected", false}, {"updated", QDateTime::currentSecsSinceEpoch()}});
    device.close();
    logInfo("\nDevice closed. Goodbye!");

    return result;
}
