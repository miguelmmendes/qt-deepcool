#include "deepcooldevice.h"
#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <linux/hidraw.h>
#include <sys/ioctl.h>

// The device answers a known command by echoing its byte; 0x00 means rejected
static bool echoes(const QByteArray &resp, quint8 command)
{
    return resp.size() >= 3 && static_cast<quint8>(resp[2]) == command;
}

DeepCoolDevice::DeepCoolDevice()
    : deviceType(DEVICE_TYPE_UNKNOWN)
    , usbContext(nullptr)
    , deviceHandle(nullptr)
    , interfaceNumber(0)
    , endpointOut(0x02)
    , endpointIn(0x82)
    , fd(-1)
    , currentMode(MODE_CPU_INFO)
    , currentRotation(ROTATION_0)
    , currentLed(LED_TEMPERATURE)
    , currentBrightness(50)
    , currentIdle(IDLE_ANIMATION)
    , currentScreen(SCREEN_CPU_TEMP)
    , currentAux(AUX_SYSTEM)
{
    libusb_init(&usbContext);
}

DeepCoolDevice::~DeepCoolDevice()
{
    close();
    if (usbContext) {
        libusb_exit(usbContext);
        usbContext = nullptr;
    }
}

bool DeepCoolDevice::open(const DeviceInfo &devInfo)
{
    if (isOpen()) {
        close();
    }

    deviceInfo = devInfo;
    deviceType = devInfo.type;
    deviceName = devInfo.displayName;

    if (devInfo.type == DEVICE_TYPE_HID) {
        fd = ::open(devInfo.devicePath.toUtf8().constData(), O_RDWR | O_NONBLOCK);
        if (fd < 0) {
            return false;
        }
    }
    else if (devInfo.type == DEVICE_TYPE_USB_VENDOR) {
        if (!usbContext) {
            return false;
        }

        libusb_device **devList;
        ssize_t cnt = libusb_get_device_list(usbContext, &devList);
        if (cnt < 0) {
            return false;
        }

        libusb_device *targetDevice = nullptr;
        for (ssize_t i = 0; i < cnt; i++) {
            struct libusb_device_descriptor desc;
            if (libusb_get_device_descriptor(devList[i], &desc) == 0) {
                if (desc.idVendor == devInfo.vendorId && desc.idProduct == devInfo.productId) {
                    targetDevice = devList[i];
                    break;
                }
            }
        }

        if (!targetDevice) {
            libusb_free_device_list(devList, 1);
            return false;
        }

        int ret = libusb_open(targetDevice, &deviceHandle);
        libusb_free_device_list(devList, 1);

        if (ret < 0) {
            return false;
        }

        if (libusb_kernel_driver_active(deviceHandle, interfaceNumber) == 1) {
            libusb_detach_kernel_driver(deviceHandle, interfaceNumber);
        }

        ret = libusb_claim_interface(deviceHandle, interfaceNumber);
        if (ret < 0) {
            close();
            return false;
        }
    }
    else {
        return false;
    }

    if (!verifyDevice()) {
        close();
        return false;
    }

    return true;
}

void DeepCoolDevice::close()
{
    if (deviceType == DEVICE_TYPE_HID && fd >= 0) {
        ::close(fd);
        fd = -1;
    }
    else if (deviceType == DEVICE_TYPE_USB_VENDOR && deviceHandle) {
        libusb_release_interface(deviceHandle, interfaceNumber);
        libusb_close(deviceHandle);
        deviceHandle = nullptr;
    }

    deviceType = DEVICE_TYPE_UNKNOWN;
}

bool DeepCoolDevice::verifyDevice()
{
    if (!isOpen()) {
        return false;
    }

    uint16_t vendor = 0;
    uint16_t product = 0;

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        struct hidraw_devinfo info;
        if (ioctl(fd, HIDIOCGRAWINFO, &info) < 0) {
            return false;
        }
        vendor = info.vendor;
        product = info.product;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        vendor = deviceInfo.vendorId;
        product = deviceInfo.productId;
    } else {
        return false;
    }

    if (vendor == DEEPCOOL_VENDOR_ID) {
        switch (product) {
            case 0x0001: deviceName = "DeepCool AK400 DIGITAL"; break;
            case 0x0002: deviceName = "DeepCool AK620 DIGITAL"; break;
            case 0x0003: deviceName = "DeepCool AK500 DIGITAL"; break;
            case 0x0004: deviceName = "DeepCool AK500S DIGITAL"; break;
            case 0x0005: deviceName = "DeepCool CH560 DIGITAL"; break;
            case 0x0006: deviceName = "DeepCool LS520/LS720 SE DIGITAL"; break;
            case 0x0007: deviceName = "DeepCool MORPHEUS"; break;
            case 0x0008: deviceName = "DeepCool AG400/AG620 DIGITAL"; break;
            case 0x0009: deviceName = "DeepCool MYSTIQUE 240/360"; break;
            case 0x000A: deviceName = "DeepCool LD240/LD360"; break;
            case 0x000C: deviceName = "DeepCool LP240/LP360"; break;
            case 0x000D: deviceName = "DeepCool LQ240/LQ360"; break;
            case 0x000F: deviceName = "DeepCool ASSASSIN IV VC VISION"; break;
            case 0x0010: deviceName = "DeepCool AK400 DIGITAL PRO"; break;
            case 0x0011: deviceName = "DeepCool AK500 DIGITAL PRO"; break;
            case 0x0012: deviceName = "DeepCool AK620 DIGITAL PRO"; break;
            case 0x0013: deviceName = "DeepCool CH170 DIGITAL"; break;
            case 0x0015: deviceName = "DeepCool CH360 DIGITAL"; break;
            case 0x0016: deviceName = "DeepCool CH270 DIGITAL"; break;
            case 0x001B: deviceName = "DeepCool CH690 DIGITAL"; break;
            default:
                deviceName = QString("DeepCool Device (PID: 0x%1)").arg(product, 4, 16, QChar('0'));
                break;
        }
        return true;
    }

    if (vendor == 0x34D3 && product == 0x1100) {
        deviceName = "DeepCool CH510 MESH DIGITAL";
        return true;
    }

    return false;
}

QString DeepCoolDevice::getDeviceInfo() const
{
    if (!isOpen()) {
        return "Device not open";
    }

    return QString("Name: %1\nVendor ID: 0x%2\nProduct ID: 0x%3\nPath: %4")
        .arg(deviceName)
        .arg(deviceInfo.vendorId, 4, 16, QChar('0'))
        .arg(deviceInfo.productId, 4, 16, QChar('0'))
        .arg(deviceInfo.devicePath);
}

bool DeepCoolDevice::sendData(const QByteArray &data)
{
    if (!isOpen()) {
        return false;
    }

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        int bytesWritten = ::write(fd, data.constData(), data.size());
        return (bytesWritten == data.size());
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        int bytesWritten = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle, endpointOut,
            (unsigned char*)data.constData(), data.size(),
            &bytesWritten, 1000
        );
        return (ret >= 0);
    }

    return false;
}

QByteArray DeepCoolDevice::receiveData(int length)
{
    if (!isOpen()) {
        return QByteArray();
    }

    QByteArray buffer(length, 0);

    if (deviceInfo.type == DEVICE_TYPE_HID) {
        int bytesRead = ::read(fd, buffer.data(), length);
        if (bytesRead < 0) {
            return QByteArray();
        }
        buffer.resize(bytesRead);
        return buffer;
    } else if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR) {
        int bytesRead = 0;
        int ret = libusb_bulk_transfer(
            deviceHandle, endpointIn,
            (unsigned char*)buffer.data(), length,
            &bytesRead, 1000
        );
        if (ret < 0) {
            return QByteArray();
        }
        buffer.resize(bytesRead);
        return buffer;
    }

    return QByteArray();
}

QByteArray DeepCoolDevice::buildPacket(quint8 command, const QByteArray &payload)
{
    QByteArray packet(PACKET_SIZE, 0);

    packet[0] = 0xAA;
    packet[1] = 0x2E;
    packet[2] = command;

    if (!payload.isEmpty() && payload.size() <= 39) {
        for (int i = 0; i < payload.size(); ++i) {
            packet[3 + i] = payload[i];
        }
    }

    packet[42] = 0x48;  // 'H'
    packet[43] = 0x49;  // 'I'
    packet[44] = 0x44;  // 'D'
    packet[45] = 0x43;  // 'C'

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<quint8>(checksum & 0xFF);
    packet[47] = static_cast<quint8>((checksum >> 8) & 0xFF);

    return packet;
}

bool DeepCoolDevice::validateResponse(const QByteArray &response)
{
    return !response.isEmpty() && response.size() >= 2;
}

bool DeepCoolDevice::sendStatusRequest()
{
    if (!isOpen()) {
        return false;
    }

    QByteArray packet(48, 0);
    packet[0] = static_cast<char>(0xAA);
    packet[1] = 0x2E;
    packet[2] = 0x10;
    packet[42] = 0x48;
    packet[43] = 0x49;
    packet[44] = 0x44;
    packet[45] = 0x43;

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(packet[i]);
    }
    packet[46] = static_cast<char>(checksum & 0xFF);
    packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

    if (!sendData(packet)) {
        return false;
    }

    receiveData(64);
    return true;
}

bool DeepCoolDevice::initMachineInfoMode()
{
    if (!isOpen()) {
        return false;
    }

    qDebug() << "Sending initialization sequence (captured from Windows)...";

    // Init sequence uses endpoint 0x01, not 0x02!
    auto sendInitPacket = [this](quint8 cmd, const QByteArray& payload) -> QByteArray {
        QByteArray packet(48, 0);
        packet[0] = static_cast<char>(0xAA);
        packet[1] = 0x2E;
        packet[2] = cmd;

        for (int i = 0; i < payload.size() && i < 39; i++) {
            packet[3 + i] = payload[i];
        }

        packet[42] = 0x48;
        packet[43] = 0x49;
        packet[44] = 0x44;
        packet[45] = 0x43;

        quint16 checksum = 0;
        for (int j = 0; j < 46; ++j) {
            checksum += static_cast<quint8>(packet[j]);
        }
        packet[46] = static_cast<char>(checksum & 0xFF);
        packet[47] = static_cast<char>((checksum >> 8) & 0xFF);

        int transferred = 0;
        libusb_bulk_transfer(deviceHandle, 0x01, (unsigned char*)packet.data(), 48, &transferred, 1000);

        QByteArray response(64, 0);
        libusb_bulk_transfer(deviceHandle, 0x81, (unsigned char*)response.data(), 64, &transferred, 1000);

        return response;
    };

    // 1. Command 0x12 - Device info
    qDebug() << "  Cmd 0x12...";
    QByteArray resp = sendInitPacket(0x12, QByteArray());
    qDebug() << "    Resp:" << resp.left(20).toHex();
    usleep(10000);

    // 2. Command 0x02 - Display settings (idle, rotation, LED mode, brightness)
    qDebug() << "  Cmd 0x02...";
    sendInitPacket(0x02, configPayload());
    usleep(10000);

    // 3-9. Setup commands
    qDebug() << "  Cmd 0x03-0x06...";
    sendInitPacket(0x03, QByteArray::fromHex("01"));  // Machine Info (stats) mode
    QByteArray layoutResp;
    {
        // Layout: <main screen> 00 00 <aux area>
        QByteArray layoutPayload(4, 0);
        layoutPayload[0] = static_cast<char>(currentScreen);
        layoutPayload[3] = static_cast<char>(currentAux);
        layoutResp = sendInitPacket(0x04, layoutPayload);
    }
    sendInitPacket(0x07, QByteArray::fromHex("0002"));
    sendInitPacket(0x08, QByteArray::fromHex("0004"));
    sendInitPacket(0x05, QByteArray::fromHex("0101"));
    sendInitPacket(0x0B, QByteArray());
    sendInitPacket(0x06, QByteArray::fromHex("01"));
    usleep(10000);

    // 10-12. Commands 0x15-0x17
    qDebug() << "  Cmd 0x15-0x17...";
    sendInitPacket(0x15, QByteArray::fromHex("2d2d"));
    sendInitPacket(0x16, QByteArray::fromHex("2d2d"));
    sendInitPacket(0x17, QByteArray::fromHex("2d2d"));
    usleep(10000);

    // 13. Clock (0x0A) goes on the data endpoint; on 0x01 the device rejects it
    qDebug() << "  Cmd 0x0A (clock)...";
    syncClock();

    // The device echoes the command byte on success (0x00 = rejected).
    // Status (0x10) is only valid on the data endpoint, so check the layout ack instead.
    return layoutResp.size() >= 3 && static_cast<quint8>(layoutResp[0]) == 0x55 &&
           static_cast<quint8>(layoutResp[2]) == 0x04;
}

bool DeepCoolDevice::setDisplayMode(DisplayMode mode)
{
    if (currentMode == mode)
        return true;
    currentMode = mode;
    // Re-run the init sequence so the device picks up the new mode payload
    return initMachineInfoMode();
}

QByteArray DeepCoolDevice::configPayload() const
{
    // 0x02: <idle> 01 <rotation> <LED mode> <brightness>  (see PROTOCOL.md)
    QByteArray payload(5, 0);
    payload[0] = static_cast<char>(currentIdle);
    payload[1] = 0x01;
    payload[2] = static_cast<char>(currentRotation & 0x03);
    payload[3] = static_cast<char>(currentLed);
    payload[4] = static_cast<char>(currentBrightness);
    return payload;
}

bool DeepCoolDevice::setRotation(ScreenRotation rotation)
{
    return setDisplaySettings(rotation, currentLed, currentBrightness, currentIdle);
}

bool DeepCoolDevice::setDisplaySettings(ScreenRotation rotation, LedMode led, int brightness, IdleMode idle)
{
    if (!isOpen()) {
        return false;
    }
    ScreenRotation oldRotation = currentRotation;
    LedMode oldLed = currentLed;
    int oldBrightness = currentBrightness;
    IdleMode oldIdle = currentIdle;

    currentRotation = rotation;
    currentLed = led;
    currentBrightness = qBound(0, brightness, 100);
    currentIdle = idle;
    if (!echoes(sendControl(CMD_CONFIG, configPayload()), CMD_CONFIG)) {
        currentRotation = oldRotation;
        currentLed = oldLed;
        currentBrightness = oldBrightness;
        currentIdle = oldIdle;
        return false;
    }
    return true;
}

QByteArray DeepCoolDevice::sendControl(quint8 command, const QByteArray &payload)
{
    QByteArray packet = buildPacket(command, payload);

    if (deviceInfo.type == DEVICE_TYPE_USB_VENDOR && deviceHandle) {
        int transferred = 0;
        int ret = libusb_bulk_transfer(deviceHandle, 0x01,
            (unsigned char*)packet.data(), packet.size(), &transferred, 1000);
        if (ret < 0) {
            qDebug() << "sendControl" << Qt::hex << command << "failed:" << libusb_error_name(ret);
            return QByteArray();
        }
        QByteArray response(64, 0);
        ret = libusb_bulk_transfer(deviceHandle, 0x81,
            (unsigned char*)response.data(), 64, &transferred, 1000);
        if (ret < 0) {
            return QByteArray();
        }
        response.resize(transferred);
        return response;
    }
    if (deviceInfo.type == DEVICE_TYPE_HID && sendData(packet)) {
        return receiveData(64);
    }
    return QByteArray();
}

QByteArray DeepCoolDevice::clockPayload()
{
    // year (LE16), month, day, hour, minute, second
    QDateTime now = QDateTime::currentDateTime();
    QByteArray payload(7, 0);
    payload[0] = static_cast<char>(now.date().year() & 0xFF);
    payload[1] = static_cast<char>((now.date().year() >> 8) & 0xFF);
    payload[2] = static_cast<char>(now.date().month());
    payload[3] = static_cast<char>(now.date().day());
    payload[4] = static_cast<char>(now.time().hour());
    payload[5] = static_cast<char>(now.time().minute());
    payload[6] = static_cast<char>(now.time().second());
    return payload;
}

bool DeepCoolDevice::syncClock()
{
    if (!isOpen()) {
        return false;
    }
    // Sent on the data endpoint (0x02), like display updates; the device echoes 0x0A
    if (!sendData(buildPacket(0x0A, clockPayload()))) {
        return false;
    }
    QByteArray resp = receiveData(64);
    return resp.size() >= 3 && static_cast<quint8>(resp[2]) == 0x0A;
}

bool DeepCoolDevice::writeControlRaw(const QByteArray &data)
{
    if (deviceInfo.type != DEVICE_TYPE_USB_VENDOR || !deviceHandle) {
        return false;
    }
    int transferred = 0;
    int ret = libusb_bulk_transfer(deviceHandle, 0x01,
        (unsigned char*)data.constData(), data.size(), &transferred, 5000);
    return ret >= 0 && transferred == data.size();
}

bool DeepCoolDevice::uploadImage(const QByteArray &jpeg)
{
    if (!isOpen() || jpeg.isEmpty()) {
        return false;
    }

    // 0x09 clears the stored list so the new image replaces it instead of joining a slideshow
    if (!echoes(sendControl(0x09, QByteArray()), 0x09) ||
        !echoes(sendControl(0x0F, QByteArray()), 0x0F)) {
        return false;
    }

    // 64-byte DCLd header (see PROTOCOL.md "Image Upload")
    quint16 jpegSum = 0;
    for (char c : jpeg) {
        jpegSum += static_cast<quint8>(c);
    }
    QByteArray header(64, 0);
    header.replace(0, 4, "DCLd");
    header[4] = 0x01;  // still image
    for (int i = 0; i < 4; ++i) {
        header[5 + i] = static_cast<char>((jpeg.size() >> (8 * i)) & 0xFF);
    }
    header[9] = static_cast<char>(jpegSum & 0xFF);
    header[10] = static_cast<char>(jpegSum >> 8);
    header.replace(20, 32, QCryptographicHash::hash(jpeg, QCryptographicHash::Md5).toHex());
    quint16 headerSum = 0;
    for (int i = 0; i < 62; ++i) {
        headerSum += static_cast<quint8>(header[i]);
    }
    header[62] = static_cast<char>(headerSum & 0xFF);
    header[63] = static_cast<char>(headerSum >> 8);

    if (!writeControlRaw(header)) {
        return false;
    }
    for (int offset = 0; offset < jpeg.size(); offset += 64) {
        if (!writeControlRaw(jpeg.mid(offset, 64))) {
            return false;
        }
    }
    QByteArray finish("dcldfinish");
    finish.resize(64, 0);
    if (!writeControlRaw(finish)) {
        return false;
    }

    sendControl(0x08, QByteArray(2, 0));
    return setImageMode(true);
}

bool DeepCoolDevice::setImageMode(bool on)
{
    return setScreenMode(on ? MODE_IMAGE : MODE_STATS);
}

bool DeepCoolDevice::setScreenMode(ScreenMode mode)
{
    if (!isOpen()) {
        return false;
    }
    return echoes(sendControl(0x03, QByteArray(1, static_cast<char>(mode))), 0x03);
}

bool DeepCoolDevice::setLayout(MainScreen screen, AuxArea aux)
{
    if (!isOpen()) {
        return false;
    }
    QByteArray payload(4, 0);
    payload[0] = static_cast<char>(screen);
    payload[3] = static_cast<char>(aux);
    QByteArray resp = sendControl(0x04, payload);
    if (resp.size() < 3 || static_cast<quint8>(resp[2]) != 0x04) {
        return false;
    }
    currentScreen = screen;
    currentAux = aux;
    return true;
}

bool DeepCoolDevice::updateDisplay(const SystemData &data)
{
    if (!isOpen()) {
        return false;
    }

    // Send status request first
    QByteArray statusPacket(48, 0);
    statusPacket[0] = static_cast<char>(0xAA);
    statusPacket[1] = 0x2E;
    statusPacket[2] = 0x10;
    statusPacket[42] = 0x48;
    statusPacket[43] = 0x49;
    statusPacket[44] = 0x44;
    statusPacket[45] = 0x43;
    quint16 statusChecksum = 0;
    for (int i = 0; i < 46; ++i) {
        statusChecksum += static_cast<quint8>(statusPacket[i]);
    }
    statusPacket[46] = static_cast<char>(statusChecksum & 0xFF);
    statusPacket[47] = static_cast<char>((statusChecksum >> 8) & 0xFF);

    if (!sendData(statusPacket)) {
        return false;
    }
    receiveData(48);

    // CPU frequency: caller-supplied, else max scaling_cur_freq across cores
    float ghz = data.cpuFreqGhz;
    if (ghz <= 0) {
        quint32 maxKhz = 0;
        QDir cpuDir("/sys/devices/system/cpu");
        const QStringList cpus = cpuDir.entryList(QStringList() << "cpu[0-9]*", QDir::Dirs);
        for (const QString& cpu : cpus) {
            QFile f(QString("/sys/devices/system/cpu/%1/cpufreq/scaling_cur_freq").arg(cpu));
            if (f.open(QIODevice::ReadOnly)) {
                maxKhz = qMax(maxKhz, QTextStream(&f).readAll().trimmed().toUInt());
            }
        }
        ghz = maxKhz / 1e6f;
    }

    // GPU_FOCUS puts GPU temp in the CPU-temp slot (main display + LED colour)
    float mainTemp = (currentMode == MODE_GPU_FOCUS) ? data.gpuTemp : data.cpuTemp;

    // Display packet: 13 fields of 3 bytes from offset 3, each
    // [uint16 LE integer part][2-digit decimal part]. See PROTOCOL.md.
    const float fields[] = {
        mainTemp,         // 0: CPU temp (CPU-temp screen, LED colour)
        data.cpuUsage,    // 1: CPU %    (System Monitor)
        data.ramUsage,    // 2: RAM %    (System Monitor)
        data.volt3v3,     // 3: 3.3 V
        data.volt5v,      // 4: 5 V
        data.volt12v,     // 5: 12 V
        ghz,              // 6: CPU GHz  (frequency screen, System Monitor)
        data.cpuFanRpm,   // 7: CPU fan RPM
        data.pumpRpm,     // 8: pump RPM
    };

    QByteArray displayPacket(48, 0);
    displayPacket[0] = static_cast<char>(0xAA);
    displayPacket[1] = 0x2E;
    displayPacket[2] = 0x01;
    for (int k = 0; k < int(sizeof(fields) / sizeof(fields[0])); ++k) {
        float value = qBound(0.0f, fields[k], 65535.99f);
        quint16 whole = static_cast<quint16>(value);
        int decimal = qRound((value - whole) * 100.0f);
        if (decimal >= 100) {   // e.g. 4.999 rounds up to 5.00
            decimal = 0;
            whole = static_cast<quint16>(qMin(whole + 1, 65535));
        }
        displayPacket[3 + 3 * k]     = static_cast<char>(whole & 0xFF);
        displayPacket[3 + 3 * k + 1] = static_cast<char>(whole >> 8);
        displayPacket[3 + 3 * k + 2] = static_cast<char>(decimal);
    }
    displayPacket[42] = 0x48;
    displayPacket[43] = 0x49;
    displayPacket[44] = 0x44;
    displayPacket[45] = 0x43;

    quint16 checksum = 0;
    for (int i = 0; i < 46; ++i) {
        checksum += static_cast<quint8>(displayPacket[i]);
    }
    displayPacket[46] = static_cast<char>(checksum & 0xFF);
    displayPacket[47] = static_cast<char>((checksum >> 8) & 0xFF);

    if (!sendData(displayPacket)) {
        return false;
    }
    receiveData(48);

    return true;
}
