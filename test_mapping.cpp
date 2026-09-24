/**
 * DeepCool display mapping test tool.
 *
 * Sends fixed, known values to the device one mode at a time and pauses
 * between each so the user can read the device screen and report back
 * what is shown where.
 *
 * Build:  (handled by CMakeLists.txt — target deepcool-test-mapping)
 * Run:    sudo ./build/bin/deepcool-test-mapping
 */

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QTextStream>
#include <QThread>
#include <cstdio>
#include <unistd.h>

#include "deepcooldevice.h"
#include "device.h"

static void println(const char *msg) {
    puts(msg);
    fflush(stdout);
}

static void waitKey(const char *prompt) {
    fputs(prompt, stdout);
    fputs("  [Press ENTER to continue]\n", stdout);
    fflush(stdout);
    getchar();
}

// Send a fully custom display packet so every byte is explicit.
// Mirrors deepcooldevice.cpp::updateDisplay but with caller-supplied values.
static bool sendPacket(DeepCoolDevice &dev,
                       quint8 byte3,  quint8 byte6,
                       quint8 byte9,  quint8 byte11,
                       quint8 byte14, quint8 byte17,
                       quint8 byte21, quint8 byte23,
                       quint16 decimal24)
{
    // Status handshake first
    QByteArray sp(48, 0);
    sp[0] = char(0xAA); sp[1] = 0x2E; sp[2] = 0x10;
    sp[42] = 0x48; sp[43] = 0x49; sp[44] = 0x44; sp[45] = 0x43;
    quint16 cs = 0;
    for (int i = 0; i < 46; i++) cs += quint8(sp[i]);
    sp[46] = char(cs & 0xFF); sp[47] = char((cs >> 8) & 0xFF);
    dev.sendData(sp);
    dev.receiveData(48);

    QByteArray p(48, 0);
    p[0]  = char(0xAA);
    p[1]  = 0x2E;
    p[2]  = 0x01;
    p[3]  = char(byte3);    // slot A
    p[6]  = char(byte6);    // slot B
    p[9]  = char(byte9);    // slot C
    p[11] = char(byte11);   // slot D
    p[12] = 0x03;
    p[14] = char(byte14);   // slot E
    p[15] = 0x05;
    p[17] = char(byte17);   // slot F
    p[18] = 0x0c;
    p[20] = 0x07;
    p[21] = char(byte21);   // slot G (integer part of bottom value)
    p[23] = char(byte23);   // slot H (decimal helper)
    p[24] = char(decimal24 & 0xFF);
    p[25] = char((decimal24 >> 8) & 0xFF);
    p[27] = char(decimal24 & 0xFF);
    p[28] = char((decimal24 >> 8) & 0xFF);
    p[42] = 0x48; p[43] = 0x49; p[44] = 0x44; p[45] = 0x43;

    cs = 0;
    for (int i = 0; i < 46; i++) cs += quint8(p[i]);
    p[46] = char(cs & 0xFF);
    p[47] = char((cs >> 8) & 0xFF);

    return dev.sendData(p);
}

int main(int argc, char *argv[])
{
    QCoreApplication app(argc, argv);
    qInstallMessageHandler([](QtMsgType, const QMessageLogContext &, const QString &) {});

    println("=== DeepCool Display Mapping Test ===\n");
    println("This tool sends known fixed values to each packet slot.");
    println("For each step, look at your device screen and note every");
    println("number/value you see, then report it back.\n");

    auto devices = Device::detectDevices();
    if (devices.isEmpty()) {
        println("ERROR: No DeepCool device found. Check USB and permissions.");
        return 1;
    }

    println("Found device:");
    printf("  %s  (%s)\n\n", devices[0].displayName.toUtf8().constData(),
                              devices[0].devicePath.toUtf8().constData());

    DeepCoolDevice dev;
    if (!dev.open(devices[0])) {
        println("ERROR: Failed to open device.");
        return 1;
    }

    if (!dev.initMachineInfoMode()) {
        println("WARNING: initMachineInfoMode returned false — continuing anyway.");
    }
    QThread::msleep(300);

    // ── Test sequence ──────────────────────────────────────────────────────
    // Each step uses a different sentinel value per slot so you can identify
    // which screen position corresponds to which packet byte.
    //
    // Slot legend (byte positions in the 48-byte packet):
    //   A = byte[3]   B = byte[6]   C = byte[9]   D = byte[11]
    //   E = byte[14]  F = byte[17]  G = byte[21]  H = byte[23]
    //   decimal24 = bytes[24-25] / [27-28]  (little-endian u16)

    // ── Step 1: All slots set to distinct values ───────────────────────────
    println("----------------------------------------");
    println("STEP 1 — Distinct sentinel values in every slot:");
    println("");
    println("  Slot A (byte 3)   = 11");
    println("  Slot B (byte 6)   = 22");
    println("  Slot C (byte 9)   = 33");
    println("  Slot D (byte 11)  = 44");
    println("  Slot E (byte 14)  = 55");
    println("  Slot F (byte 17)  = 66");
    println("  Slot G (byte 21)  = 77  (integer)");
    println("  Slot H (byte 23)  = 88");
    println("  decimal24         = 9900 (0x26AC)");
    println("");
    println("Look at the device screen and note every value shown.");
    sendPacket(dev, 11, 22, 33, 44, 55, 66, 77, 88, 9900);
    waitKey("");

    // ── Step 2: Isolate — only slot A non-zero ─────────────────────────────
    println("----------------------------------------");
    println("STEP 2 — Only slot A = 72, all others = 0:");
    println("");
    println("  Slot A (byte 3)  = 72   <-- only this one");
    println("  Everything else  = 0");
    println("");
    println("Which screen position shows 72?");
    sendPacket(dev, 72, 0, 0, 0, 0, 0, 0, 0, 0);
    waitKey("");

    // ── Step 3: Isolate — only slot B non-zero ─────────────────────────────
    println("----------------------------------------");
    println("STEP 3 — Only slot B = 88, all others = 0:");
    println("");
    println("  Slot B (byte 6)  = 88   <-- only this one");
    println("");
    println("Which screen position changes to 88?");
    sendPacket(dev, 0, 88, 0, 0, 0, 0, 0, 0, 0);
    waitKey("");

    // ── Step 4: Isolate — only slot C non-zero ─────────────────────────────
    println("----------------------------------------");
    println("STEP 4 — Only slot C = 50, all others = 0:");
    println("");
    println("  Slot C (byte 9)  = 50   <-- only this one");
    println("");
    sendPacket(dev, 0, 0, 50, 0, 0, 0, 0, 0, 0);
    waitKey("");

    // ── Step 5: Isolate — only slot E non-zero ─────────────────────────────
    println("----------------------------------------");
    println("STEP 5 — Only slot E = 65, all others = 0:");
    println("");
    println("  Slot E (byte 14) = 65   <-- only this one");
    println("");
    sendPacket(dev, 0, 0, 0, 0, 65, 0, 0, 0, 0);
    waitKey("");

    // ── Step 6: Isolate — only slot G + decimal non-zero ──────────────────
    println("----------------------------------------");
    println("STEP 6 — Only slot G = 3  and  decimal24 = 5000:");
    println("");
    println("  Slot G (byte 21)  = 3");
    println("  decimal24         = 5000");
    println("  (Together these might display as '3.50' or similar)");
    println("");
    sendPacket(dev, 0, 0, 0, 0, 0, 0, 3, 0, 5000);
    waitKey("");

    // ── Step 7: Realistic values — CPU mode ───────────────────────────────
    println("----------------------------------------");
    println("STEP 7 — Realistic CPU-mode values:");
    println("");
    println("  byte3  = 72   (intended: CPU temp °C main display)");
    println("  byte6  = 45   (intended: CPU usage %)");
    println("  byte9  = 60   (intended: RAM usage %)");
    println("  byte14 = 58   (intended: GPU temp °C)");
    println("  byte21 = 3    (intended: GHz integer)");
    println("  decimal24 = 5000 (intended: .50 GHz fraction → 3.50 GHz)");
    println("");
    println("Does the main (large) number show 72? Does a % bar show 45?");
    println("Report exactly what each position on screen shows.");
    // byte11=9 (typical), byte17=6 (typical), byte23=50 (decimal helper)
    sendPacket(dev, 72, 45, 60, 9, 58, 6, 3, 50, 5000);
    waitKey("");

    dev.close();
    println("Done. Please report what you saw on the device screen for each step.");
    return 0;
}
