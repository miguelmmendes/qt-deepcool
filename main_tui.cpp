#include <atomic>
#include <csignal>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QLoggingCategory>
#include <QMessageLogContext>
#include <QRegularExpression>
#include <QTextStream>
#include <QVector>

#include "deepcooldevice.h"
#include "device.h"

#include "ftxui/component/component.hpp"
#include "ftxui/component/component_base.hpp"
#include "ftxui/component/event.hpp"
#include "ftxui/component/screen_interactive.hpp"
#include "ftxui/dom/elements.hpp"

using namespace ftxui;

// ── Sensor readings ──────────────────────────────────────────────────────────

static long long prev_idle  = 0;
static long long prev_total = 0;

static float getCPUTemperature(bool useFahrenheit) {
    float celsius = 0.0f;
    QDir hwmonDir("/sys/class/hwmon");
    const QStringList hwmons = hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot);
    for (const QString &hwmon : hwmons) {
        QFile nameFile(QString("/sys/class/hwmon/%1/name").arg(hwmon));
        if (!nameFile.open(QIODevice::ReadOnly)) continue;
        const QString name = QTextStream(&nameFile).readAll().trimmed();
        nameFile.close();
        if (name != "coretemp" && name != "k10temp") continue;
        float maxTemp = 0.0f;
        for (int i = 1; i <= 20; i++) {
            QFile tf(QString("/sys/class/hwmon/%1/temp%2_input").arg(hwmon).arg(i));
            if (!tf.open(QIODevice::ReadOnly)) continue;
            float t = QTextStream(&tf).readAll().trimmed().toFloat() / 1000.0f;
            tf.close();
            if (t > maxTemp && t < 150) maxTemp = t;
        }
        if (maxTemp > 0) { celsius = maxTemp; break; }
    }
    if (celsius <= 0) {
        for (const auto &path : {"/sys/class/thermal/thermal_zone0/temp",
                                  "/sys/class/thermal/thermal_zone1/temp",
                                  "/sys/class/thermal/thermal_zone2/temp"}) {
            QFile tf(path);
            if (!tf.open(QIODevice::ReadOnly)) continue;
            float t = QTextStream(&tf).readAll().trimmed().toFloat() / 1000.0f;
            tf.close();
            if (t > celsius && t < 150) celsius = t;
        }
    }
    if (celsius > 0 && useFahrenheit) return celsius * 9.0f / 5.0f + 32.0f;
    return celsius;
}

static float getCPUUsage() {
    QFile statFile("/proc/stat");
    if (!statFile.open(QIODevice::ReadOnly)) return 0.0f;
    const QString line = QTextStream(&statFile).readLine();
    statFile.close();
    if (!line.startsWith("cpu ")) return 0.0f;
    const QStringList parts = line.split(QRegularExpression("\\s+"), Qt::SkipEmptyParts);
    if (parts.size() < 5) return 0.0f;
    const long long user    = parts[1].toLongLong();
    const long long nice    = parts[2].toLongLong();
    const long long system  = parts[3].toLongLong();
    const long long idle    = parts[4].toLongLong();
    const long long iowait  = parts.size() > 5 ? parts[5].toLongLong() : 0;
    const long long irq     = parts.size() > 6 ? parts[6].toLongLong() : 0;
    const long long softirq = parts.size() > 7 ? parts[7].toLongLong() : 0;
    const long long steal   = parts.size() > 8 ? parts[8].toLongLong() : 0;
    const long long total_idle  = idle + iowait;
    const long long total       = user + nice + system + idle + iowait + irq + softirq + steal;
    float usage = 0.0f;
    if (prev_total > 0) {
        const long long diff_idle  = total_idle - prev_idle;
        const long long diff_total = total - prev_total;
        if (diff_total > 0)
            usage = (1.0f - (float)diff_idle / (float)diff_total) * 100.0f;
    }
    prev_idle  = total_idle;
    prev_total = total;
    return usage;
}

static float getGPUTemperature(bool useFahrenheit) {
    float celsius = 0.0f;
    if (QFile("/usr/bin/nvidia-smi").exists()) {
        FILE *pipe = popen("nvidia-smi --query-gpu=temperature.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buf[128];
            if (fgets(buf, sizeof(buf), pipe)) celsius = atof(buf);
            pclose(pipe);
            if (celsius > 0) {
                if (useFahrenheit) return celsius * 9.0f / 5.0f + 32.0f;
                return celsius;
            }
        }
    }
    QDir hwmonDir("/sys/class/hwmon");
    for (const QString &hwmon : hwmonDir.entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        QFile nf(QString("/sys/class/hwmon/%1/name").arg(hwmon));
        if (!nf.open(QIODevice::ReadOnly)) continue;
        const QString name = QTextStream(&nf).readAll().trimmed();
        nf.close();
        if (name != "amdgpu" && name != "radeon") continue;
        QFile tf(QString("/sys/class/hwmon/%1/temp1_input").arg(hwmon));
        if (!tf.open(QIODevice::ReadOnly)) continue;
        celsius = QTextStream(&tf).readAll().trimmed().toFloat() / 1000.0f;
        tf.close();
        if (celsius > 0) {
            if (useFahrenheit) return celsius * 9.0f / 5.0f + 32.0f;
            return celsius;
        }
    }
    return 0.0f;
}

static float getGPUUsage() {
    if (QFile("/usr/bin/nvidia-smi").exists()) {
        FILE *pipe = popen("nvidia-smi --query-gpu=utilization.gpu --format=csv,noheader,nounits 2>/dev/null", "r");
        if (pipe) {
            char buf[128];
            float usage = 0.0f;
            if (fgets(buf, sizeof(buf), pipe)) usage = atof(buf);
            pclose(pipe);
            if (usage >= 0) return usage;
        }
    }
    QFile amdUsage("/sys/class/drm/card0/device/gpu_busy_percent");
    if (amdUsage.open(QIODevice::ReadOnly))
        return QTextStream(&amdUsage).readAll().trimmed().toFloat();
    return 0.0f;
}

static float getRAMUsage() {
    QFile f("/proc/meminfo");
    if (!f.open(QIODevice::ReadOnly)) return 0.0f;
    const QString content = QTextStream(&f).readAll();
    f.close();
    qint64 memTotal = 0, memAvailable = 0;
    const QRegularExpression ws("\\s+");
    for (const QString &line : content.split('\n')) {
        if (line.startsWith("MemTotal:"))     memTotal     = line.split(ws)[1].toLongLong();
        else if (line.startsWith("MemAvailable:")) memAvailable = line.split(ws)[1].toLongLong();
    }
    if (memTotal > 0) return ((memTotal - memAvailable) / (float)memTotal) * 100.0f;
    return 0.0f;
}

// ── App state ─────────────────────────────────────────────────────────────────

struct AppState {
    QVector<DeviceInfo> devices;
    int  selectedDevice  = 0;
    bool connected       = false;
    bool monitoring      = false;
    float cpuTemp        = 0.0f;
    float cpuUsage       = 0.0f;
    float gpuTemp        = 0.0f;
    float gpuUsage       = 0.0f;
    float ramUsage       = 0.0f;
    int  displayMode     = 0;
    int  rotation        = 0;
    int  interval        = 1000;
    bool useFahrenheit   = false;
    std::string statusMsg = "Not connected";
    DeepCoolDevice *device = nullptr;
};

static std::mutex          g_mutex;
static std::atomic<bool>   g_stop{false};
static ScreenInteractive  *g_screen = nullptr;

static void signalHandler(int) {
    g_stop = true;
    if (g_screen) g_screen->ExitLoopClosure()();
}

// ── Gauge row helper ──────────────────────────────────────────────────────────

static Element gaugeRow(const std::string &label, float value, float maxVal,
                        const std::string &valStr) {
    const float ratio = (maxVal > 0) ? std::min(value / maxVal, 1.0f) : 0.0f;
    return hbox({
        text(label)  | size(WIDTH, EQUAL, 6),
        gauge(ratio) | flex | color(Color::Cyan),
        text("  " + valStr) | size(WIDTH, EQUAL, 14),
    });
}

// ── Main ──────────────────────────────────────────────────────────────────────

int main(int argc, char *argv[]) {
    QCoreApplication app(argc, argv);

    // Silence all Qt debug/warning output — it would corrupt the TUI screen
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});

    signal(SIGINT,  signalHandler);
    signal(SIGTERM, signalHandler);

    AppState state;
    state.devices = Device::detectDevices();

    // ── Component state (must outlive the component tree) ──
    std::vector<std::string> deviceEntries;
    for (const DeviceInfo &d : state.devices)
        deviceEntries.push_back(d.displayName.toStdString());
    if (deviceEntries.empty()) deviceEntries.push_back("No devices found");

    std::vector<std::string> modeEntries   = {"CPU Info", "GPU Info", "System Overview", "Custom", "GPU Focus"};
    std::vector<std::string> rotEntries    = {"0°", "90°", "180°", "270°"};
    std::vector<std::string> unitEntries   = {"°C", "°F"};
    std::string              intervalStr   = "1000";

    int  deviceSel  = 0;
    int  modeSel    = 0;
    int  rotSel     = 0;
    int  unitSel    = 0;  // 0=°C, 1=°F

    // ── Components ────────────────────────────────────────
    auto deviceDropdown   = Dropdown(&deviceEntries, &deviceSel);
    auto modeDropdown     = Dropdown(&modeEntries,   &modeSel);
    auto rotDropdown      = Dropdown(&rotEntries,    &rotSel);
    auto unitToggle       = Toggle(&unitEntries,     &unitSel);
    auto intervalInput    = Input(&intervalStr, "interval ms");

    auto connectBtn = Button("Connect", [&] {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (state.connected || state.devices.isEmpty()) return;
        if (!state.device) state.device = new DeepCoolDevice();
        if (state.device->open(state.devices[deviceSel])) {
            state.device->initMachineInfoMode();
            state.connected  = true;
            state.statusMsg  = "Connected — " + state.devices[deviceSel].displayName.toStdString();
        } else {
            state.statusMsg = "Failed to open device (check permissions)";
        }
    });

    auto disconnectBtn = Button("Disconnect", [&] {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!state.connected) return;
        state.monitoring = false;
        if (state.device) { state.device->close(); }
        state.connected = false;
        state.statusMsg = "Disconnected";
    });

    auto refreshBtn = Button("Refresh", [&] {
        std::lock_guard<std::mutex> lk(g_mutex);
        state.devices = Device::detectDevices();
        deviceEntries.clear();
        for (const DeviceInfo &d : state.devices)
            deviceEntries.push_back(d.displayName.toStdString());
        if (deviceEntries.empty()) deviceEntries.push_back("No devices found");
        deviceSel = 0;
        state.statusMsg = "Devices refreshed";
    });

    auto startBtn = Button("Start Monitoring", [&] {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (!state.connected) return;
        state.monitoring = true;
        state.statusMsg  = "Monitoring…";
    });

    auto stopBtn = Button("Stop Monitoring", [&] {
        std::lock_guard<std::mutex> lk(g_mutex);
        state.monitoring = false;
        state.statusMsg  = "Monitoring stopped";
    });

    auto quitBtn = Button("Quit [q]", [&] {
        g_stop = true;
        if (g_screen) g_screen->ExitLoopClosure()();
    });

    // ── Renderer ──────────────────────────────────────────
    auto renderer = Renderer(
        Container::Vertical({
            Container::Horizontal({ deviceDropdown, refreshBtn, connectBtn, disconnectBtn }),
            Container::Horizontal({ modeDropdown, rotDropdown }),
            Container::Horizontal({ intervalInput, unitToggle }),
            Container::Horizontal({ startBtn, stopBtn, quitBtn }),
        }),
        [&]() -> Element {
            std::string statusMsg;
            float cpuTemp, cpuUsage, gpuTemp, gpuUsage, ramUsage;
            bool  fahrenheit;
            bool  connected, monitoring;
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                statusMsg  = state.statusMsg;
                cpuTemp    = state.cpuTemp;
                cpuUsage   = state.cpuUsage;
                gpuTemp    = state.gpuTemp;
                gpuUsage   = state.gpuUsage;
                ramUsage   = state.ramUsage;
                fahrenheit = state.useFahrenheit;
                connected  = state.connected;
                monitoring = state.monitoring;
            }

            const std::string unit = fahrenheit ? "°F" : "°C";
            const float       tMax = fahrenheit ? 212.0f : 100.0f;

            auto cpuTempStr = std::to_string((int)cpuTemp) + unit + "  " +
                              std::to_string((int)cpuUsage) + "%";
            auto gpuTempStr = std::to_string((int)gpuTemp) + unit + "  " +
                              std::to_string((int)gpuUsage) + "%";
            auto ramStr     = std::to_string((int)ramUsage) + "%";

            // Device row
            auto deviceRow = hbox({
                text("Device: ") | bold,
                deviceDropdown->Render() | size(WIDTH, EQUAL, 30),
                text(" "),
                refreshBtn->Render(),
                text(" "),
                connectBtn->Render(),
                text(" "),
                disconnectBtn->Render(),
            });

            // Status row
            Color statusColor = connected ? (monitoring ? Color::Blue : Color::Green) : Color::Red;
            auto statusRow = hbox({
                text("Status: ") | bold,
                text(statusMsg) | color(statusColor),
            });

            // Stats pane
            auto statsPane = vbox({
                gaugeRow("CPU   ", cpuUsage, 100.0f, cpuTempStr),
                gaugeRow("GPU   ", gpuUsage, 100.0f, gpuTempStr),
                gaugeRow("RAM   ", ramUsage, 100.0f, ramStr),
            }) | border;

            // Settings pane
            auto settingsPane = vbox({
                hbox({ text("Display Mode: ") | size(WIDTH, EQUAL, 16), modeDropdown->Render() }),
                hbox({ text("Rotation:     ") | size(WIDTH, EQUAL, 16), rotDropdown->Render() }),
                hbox({ text("Interval (ms):") | size(WIDTH, EQUAL, 16), intervalInput->Render() | size(WIDTH, EQUAL, 10) }),
                hbox({ text("Units:        ") | size(WIDTH, EQUAL, 16), unitToggle->Render() }),
            }) | border;

            // Buttons row
            auto btnRow = hbox({
                startBtn->Render(), text(" "),
                stopBtn->Render(),  text(" "),
                quitBtn->Render(),
            });

            return window(
                text(" DeepCool Controller "),
                vbox({
                    deviceRow,
                    statusRow,
                    separator(),
                    statsPane,
                    settingsPane,
                    btnRow,
                }) | xflex
            );
        }
    );

    // Catch 'q' globally
    auto wrappedRenderer = CatchEvent(renderer, [&](Event event) -> bool {
        if (event == Event::Character('q')) {
            g_stop = true;
            if (g_screen) g_screen->ExitLoopClosure()();
            return true;
        }
        return false;
    });

    // ── Screen & update thread ─────────────────────────────
    auto screen = ScreenInteractive::Fullscreen();
    g_screen = &screen;

    // Initial CPU sample
    getCPUUsage();

    std::thread updateThread([&] {
        while (!g_stop) {
            // Collect interval and settings under lock
            int  sleepMs;
            bool doMonitor, fahrenheit;
            int  modeSel_local, rotSel_local, unitSel_local;
            std::string intervalStr_local;
            {
                std::lock_guard<std::mutex> lk(g_mutex);
                sleepMs          = state.interval;
                doMonitor        = state.monitoring;
                fahrenheit       = state.useFahrenheit;
                modeSel_local    = modeSel;
                rotSel_local     = rotSel;
                unitSel_local    = unitSel;
                intervalStr_local = intervalStr;
            }

            // Parse interval input
            try {
                int parsed = std::stoi(intervalStr_local);
                if (parsed >= 100 && parsed <= 10000) sleepMs = parsed;
            } catch (...) {}

            // Apply settings changes + read sensors
            float cpuTemp  = getCPUTemperature(fahrenheit);
            float cpuUsage = getCPUUsage();
            float gpuTemp  = getGPUTemperature(fahrenheit);
            float gpuUsage = getGPUUsage();
            float ramUsage = getRAMUsage();

            {
                std::lock_guard<std::mutex> lk(g_mutex);
                state.interval      = sleepMs;
                state.useFahrenheit = (unitSel_local == 1);
                state.cpuTemp       = cpuTemp;
                state.cpuUsage      = cpuUsage;
                state.gpuTemp       = gpuTemp;
                state.gpuUsage      = gpuUsage;
                state.ramUsage      = ramUsage;

                // Push mode/rotation changes to device
                if (state.connected && state.device) {
                    state.device->setDisplayMode(static_cast<DisplayMode>(modeSel_local));
                    state.device->setRotation(static_cast<ScreenRotation>(rotSel_local));
                }

                // Send data to cooler display if monitoring
                if (doMonitor && state.connected && state.device) {
                    SystemData data;
                    data.cpuTemp       = cpuTemp;
                    data.cpuUsage      = cpuUsage;
                    data.gpuTemp       = gpuTemp;
                    data.gpuUsage      = gpuUsage;
                    data.ramUsage      = ramUsage;
                    data.useFahrenheit = state.useFahrenheit;
                    state.device->updateDisplay(data);
                }
            }

            screen.PostEvent(Event::Custom);

            std::this_thread::sleep_for(std::chrono::milliseconds(sleepMs));
        }
    });

    screen.Loop(wrappedRenderer);

    g_stop = true;
    updateThread.join();

    {
        std::lock_guard<std::mutex> lk(g_mutex);
        if (state.device) {
            state.device->close();
            delete state.device;
        }
    }

    return 0;
}
