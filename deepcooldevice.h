#ifndef DEEPCOOLDEVICE_H
#define DEEPCOOLDEVICE_H

#include <QString>
#include <QByteArray>
#include <libusb-1.0/libusb.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>
#include "device.h"

enum DisplayMode {
    MODE_CPU_INFO = 0,      // CPU temp main, CPU GHz bottom
    MODE_GPU_INFO = 1,      // CPU temp main, GPU temp bottom
    MODE_SYSTEM_OVERVIEW = 2,
    MODE_CUSTOM = 3,
    MODE_GPU_FOCUS = 4      // GPU temp main (with LED), CPU temp bottom
};
enum ScreenRotation {
    ROTATION_0   = 0,   // 0° (no rotation)
    ROTATION_90  = 1,   // 90° clockwise
    ROTATION_180 = 2,   // 180°
    ROTATION_270 = 3    // 270° clockwise
};


// Built-in firmware screens, selected with command 0x04 (see PROTOCOL.md)
enum MainScreen {
    SCREEN_CPU_FREQ = 0,    // CPU frequency (GHz)
    SCREEN_CLOCK    = 1,    // Clock, set with syncClock()
    SCREEN_PUMP     = 2,    // Pump speed (RPM)
    SCREEN_CPU_FAN  = 3,    // CPU / radiator fan speed (RPM)
    SCREEN_FANS     = 4,    // CPU fan + pump combined
    SCREEN_CPU_TEMP = 5     // CPU temperature (also drives the LED ring colour)
};
enum AuxArea {
    AUX_VOLTAGES = 0,       // 3.3 V / 5 V / 12 V
    AUX_SYSTEM   = 1,       // GHz / CPU % / RAM %
    AUX_CORE     = 2        // "Core Data": CPU temp / GHz
};

struct SystemData {
    float cpuTemp = 0;
    float cpuUsage = 0;
    float gpuTemp = 0;
    float gpuUsage = 0;
    float ramUsage = 0;
    bool useFahrenheit = false;
    float cpuFreqGhz = 0;   // 0 = read max scaling_cur_freq from sysfs
    float cpuFanRpm = 0;
    float pumpRpm = 0;
    float volt3v3 = 0;
    float volt5v = 0;
    float volt12v = 0;
};

class DeepCoolDevice : public Device
{
public:
    DeepCoolDevice();
    ~DeepCoolDevice() override;

    // Implement Device interface
    bool open(const DeviceInfo &deviceInfo) override;
    void close() override;
    bool isOpen() const override {
        return (deviceType == DEVICE_TYPE_HID && fd >= 0) ||
               (deviceType == DEVICE_TYPE_USB_VENDOR && deviceHandle != nullptr);
    }

    bool sendData(const QByteArray &data) override;
    QByteArray receiveData(int length) override;
    QString getDeviceName() const override { return deviceName; }
    QString getDeviceInfo() const override;

    // High-level DeepCool-specific commands
    bool sendStatusRequest();  // Handshake/init
    bool initMachineInfoMode();  // Try to switch device to Machine Info mode
    bool setDisplayMode(DisplayMode mode);
    bool updateDisplay(const SystemData &data);
    bool setRotation(ScreenRotation rotation);
    ScreenRotation getRotation() const { return currentRotation; }
    bool setLayout(MainScreen screen, AuxArea aux);  // Switch built-in screen (cmd 0x04)
    bool syncClock();                                // Set device clock to local time (cmd 0x0A)

    // Device verification
    bool verifyDevice();

private:
    DeviceType deviceType;

    // For USB vendor-specific devices
    libusb_context *usbContext;
    libusb_device_handle *deviceHandle;
    int interfaceNumber;
    int endpointOut;
    int endpointIn;

    // For HID devices
    int fd;  // File descriptor for HID device

    // Common
    QString devicePath;
    QString deviceName;
    DisplayMode currentMode;
    ScreenRotation currentRotation;
    MainScreen currentScreen;
    AuxArea currentAux;

    // Protocol helpers
    QByteArray buildPacket(quint8 command, const QByteArray &payload);
    QByteArray sendControl(quint8 command, const QByteArray &payload);  // EP 0x01 -> reply on 0x81
    static QByteArray clockPayload();
    bool validateResponse(const QByteArray &response);

    // Command bytes (reverse-engineered from USB capture)
    static const quint8 CMD_UPDATE_DISPLAY = 0x01;  // Send display data
    static const quint8 CMD_CONFIG = 0x02;           // Configuration (rotation, etc.)
    static const quint8 CMD_STATUS_REQUEST = 0x10;   // Status/handshake request

    static const int PACKET_SIZE = 48;  // MYSTIQUE uses 48-byte packets
};

#endif // DEEPCOOLDEVICE_H
