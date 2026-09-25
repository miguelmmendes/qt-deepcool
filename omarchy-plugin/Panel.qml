import QtQuick
import QtQuick.Controls
import Quickshell
import Quickshell.Io
import qs.Commons
import qs.Ui

// DeepCool MYSTIQUE screen control.
// The deepcool-cli service owns the USB device; this widget only reads its status file and
// writes the control file it watches (see qt-deepcool README, "Omarchy plugin").
Panel {
  id: root
  moduleName: "bulletazz.deepcool"
  ipcTarget: "bulletazz.deepcool"

  readonly property color foreground: bar ? bar.foreground : Color.foreground
  readonly property color urgent: bar ? bar.urgent : Color.urgent
  readonly property color dim: Qt.darker(foreground, 1.55)
  readonly property string fontFamily: bar ? bar.fontFamily : Style.font.family

  readonly property string home: Quickshell.env("HOME")
  readonly property string runtimeDir: Quickshell.env("XDG_RUNTIME_DIR") || "/tmp"
  readonly property string statusPath: runtimeDir + "/deepcool/status.json"
  readonly property string controlDir: home + "/.config/deepcool"
  readonly property string controlPath: controlDir + "/control.json"
  readonly property string serviceName: "deepcool.service"

  // Built-in firmware screens and bottom areas (ids match deepcool-cli --layout / --aux)
  readonly property var screenIds: ["cpu-temp", "cpu-freq", "pump", "cpu-fan", "fans", "clock"]
  readonly property var screenLabels: ({
    "cpu-temp": "CPU temperature", "cpu-freq": "CPU frequency", "pump": "Pump speed",
    "cpu-fan": "CPU fan speed", "fans": "CPU fan + pump", "clock": "Clock"
  })
  readonly property var auxIds: ["system", "core", "voltages"]
  readonly property var auxLabels: ({
    "system": "GHz / CPU % / RAM %", "core": "CPU temp / GHz", "voltages": "3.3 V / 5 V / 12 V"
  })
  readonly property var rotations: ["0°", "90°", "180°", "270°"]
  readonly property var modeLabels: ({ "stats": "Stats", "image": "Picture", "history": "History" })
  readonly property var ledLabels: ({ "temperature": "Temperature", "motherboard": "Motherboard", "picture": "Picture edge" })
  readonly property var idleLabels: ({ "off": "Screen off", "animation": "Animation" })
  readonly property var swatches: ["#ff3b30", "#ff9500", "#ffcc00", "#34c759", "#00c7be", "#0a84ff", "#5e5ce6", "#ff2d92", "#ffffff"]

  property var status: ({})
  property var ctl: null        // desired settings; seeded from the status file, then local
  property real nowSec: Date.now() / 1000
  readonly property bool serviceUp: status.connected === true && nowSec - (status.updated || 0) < 5
  readonly property var settingsNow: ctl || {
    mode: status.mode || "stats",
    screens: status.screens || ["cpu-temp"],
    aux: status.aux || "system",
    cycle: status.cycle || 10,
    image: status.image || "",
    rotation: status.rotation || 0,
    brightness: status.brightness !== undefined ? status.brightness : 50,
    led: status.led || "temperature",
    ledColor: status.ledColor || "",
    idle: status.idle || "animation"
  }
  readonly property bool rotating: settingsNow.screens.length > 1
  readonly property string barLabel: serviceUp ? "󰈐 " + Math.round(status.cpuTemp || 0) + "°" : "󰈐"

  function labelsFor(ids, map) {
    return ids.map(function(id) { return map[id] })
  }

  function idFor(label, map) {
    for (var id in map) if (map[id] === label) return id
    return ""
  }

  function apply(changes) {
    var next = Object.assign({}, settingsNow, changes)
    root.ctl = next
    controlFile.setText(JSON.stringify(next, null, 2) + "\n")
  }

  function fmt(value, digits, suffix) {
    return (value === undefined || value === null) ? "–" : Number(value).toFixed(digits) + suffix
  }

  function fileName(path) {
    return path ? path.split("/").pop() : "No picture chosen"
  }

  FileView {
    id: statusFile
    path: root.statusPath
    watchChanges: true
    printErrors: false
    onLoaded: {
      try { root.status = JSON.parse(text()) } catch (e) { }
    }
    onFileChanged: reload()
  }

  FileView {
    id: controlFile
    path: root.controlPath
    atomicWrites: true
    printErrors: false
  }

  // The status file is replaced every second; poll too, in case a rename slips past the watcher
  Timer {
    interval: 2000
    running: true
    repeat: true
    onTriggered: {
      root.nowSec = Date.now() / 1000
      statusFile.reload()
    }
  }

  Process {
    id: ensureDirProc
    command: ["mkdir", "-p", root.controlDir]
  }

  Process {
    id: startServiceProc
    command: ["systemctl", "--user", "start", root.serviceName]
  }

  Process {
    id: pickImageProc
    command: ["zenity", "--file-selection", "--title=Choose a picture for the cooler screen",
              "--file-filter=Images | *.png *.jpg *.jpeg *.webp *.bmp *.gif"]
    stdout: StdioCollector {
      id: pickStdout
      waitForEnd: true
    }
    onExited: function(exitCode) {
      var path = String(pickStdout.text || "").trim()
      if (exitCode === 0 && path !== "") root.apply({ mode: "image", image: path })
    }
  }

  Process {
    id: pickColorProc
    command: ["zenity", "--color-selection", "--title=LED ring colour"]
    stdout: StdioCollector {
      id: colorStdout
      waitForEnd: true
    }
    onExited: function(exitCode) {
      // zenity prints rgb(r,g,b) or #rrggbb
      var out = String(colorStdout.text || "").trim()
      var m = out.match(/rgba?\((\d+),\s*(\d+),\s*(\d+)/)
      if (m) out = "#" + [m[1], m[2], m[3]].map(function(v) { return ("0" + Number(v).toString(16)).slice(-2) }).join("")
      if (exitCode === 0 && /^#[0-9a-fA-F]{6}$/.test(out)) root.setLedColor(out)
    }
  }

  // Ring colour comes from the picture's edge, so a colour means: picture mode + a painted border
  function setLedColor(color) {
    root.apply({ led: "picture", ledColor: color, mode: "image" })
  }

  Component.onCompleted: ensureDirProc.running = true

  implicitWidth: button.implicitWidth
  implicitHeight: button.implicitHeight

  // WidgetButton (not BarIconButton) so the width follows the temperature text
  WidgetButton {
    id: button
    anchors.fill: parent
    bar: root.bar
    text: root.barLabel
    tooltipText: root.serviceUp
      ? "Cooler: CPU " + root.fmt(root.status.cpuTemp, 0, "°C") + ", pump " + root.fmt(root.status.pumpRpm, 0, " rpm")
      : "DeepCool service not running"
    onPressed: function(buttonCode) { root.toggle() }
  }

  KeyboardPanel {
    id: panel
    anchorItem: button
    owner: root
    bar: root.bar
    open: root.opened
    focusTarget: keyCatcher
    contentWidth: panel.fittedContentWidth(Style.space(400))
    contentHeight: panel.fittedContentHeight(column.implicitHeight, Style.space(760))

    PanelKeyCatcher {
      id: keyCatcher
      anchors.fill: parent
      onCloseRequested: root.close()
      onTabRequested: function(direction) { root.switchPanel(direction) }

      Flickable {
        id: flick
        anchors.fill: parent
        contentWidth: width
        contentHeight: column.implicitHeight
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        interactive: contentHeight > height

        Column {
          id: column
          width: flick.width
          spacing: Style.space(12)

          PanelHero {
            width: parent.width
            title: "DeepCool MYSTIQUE"
            meta: root.serviceUp
              ? ({ "image": "Showing a picture", "history": "Showing history graphs" }[root.settingsNow.mode] || "Showing live stats")
              : "Service not running"
            foreground: root.foreground
            fontFamily: root.fontFamily
            iconComponent: Component {
              Text {
                text: "󰈐"
                color: root.serviceUp ? root.foreground : root.dim
                font.family: root.fontFamily
                font.pixelSize: Style.font.display
              }
            }
          }

          // Service not running: offer to start it
          Row {
            visible: !root.serviceUp
            spacing: Style.space(10)
            Button {
              text: "Start service"
              bordered: true
              foreground: root.foreground
              onClicked: startServiceProc.running = true
            }
            Text {
              anchors.verticalCenter: parent.verticalCenter
              text: "systemctl --user start " + root.serviceName
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
            }
          }

          Text {
            visible: (root.status.error || "") !== ""
            width: parent.width
            text: root.status.error || ""
            color: root.urgent
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          // Live readings
          Text {
            visible: root.serviceUp
            width: parent.width
            text: "CPU " + root.fmt(root.status.cpuTemp, 0, "°") + "  ·  " + root.fmt(root.status.cpuUsage, 0, "%")
                  + "  ·  RAM " + root.fmt(root.status.ramUsage, 0, "%")
                  + "\nPump " + root.fmt(root.status.pumpRpm, 0, " rpm") + "  ·  Fan " + root.fmt(root.status.cpuFanRpm, 0, " rpm")
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            lineHeight: 1.3
          }

          PanelSeparator { width: parent.width }

          PanelSectionHeader { text: "Mode"; foreground: root.foreground; fontFamily: root.fontFamily }
          ButtonGroup {
            options: ["Stats", "Picture", "History"]
            value: root.modeLabels[root.settingsNow.mode] || "Stats"
            foreground: root.foreground
            fontFamily: root.fontFamily
            onChanged: function(v) {
              var mode = root.idFor(v, root.modeLabels)
              if (mode === "image" && !root.settingsNow.image && !root.settingsNow.ledColor)
                pickImageProc.running = true
              else
                root.apply({ mode: mode })
            }
          }
          Text {
            visible: root.settingsNow.mode === "history"
            width: parent.width
            text: "Graphs of CPU frequency and temperature, drawn by the cooler."
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }

          // ---- Stats mode ----
          Column {
            visible: root.settingsNow.mode !== "image"
            width: parent.width
            spacing: Style.space(12)

            Dropdown {
              visible: !root.rotating
              width: parent.width
              label: "Screen"
              fontFamily: root.fontFamily
              options: root.labelsFor(root.screenIds, root.screenLabels)
              value: root.screenLabels[root.settingsNow.screens[0]] || ""
              onChanged: function(v) { root.apply({ screens: [root.idFor(v, root.screenLabels)] }) }
            }

            Row {
              spacing: Style.space(10)
              ToggleSwitch {
                checked: root.rotating
                foreground: root.foreground
                onToggled: root.apply({
                  screens: root.rotating ? [root.settingsNow.screens[0]]
                                         : ["cpu-temp", "cpu-freq", "pump", "cpu-fan", "clock"]
                })
              }
              Text {
                anchors.verticalCenter: parent.verticalCenter
                text: "Rotate between screens"
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
              }
            }

            MultiSelect {
              visible: root.rotating
              width: parent.width
              label: "Screens to rotate"
              options: root.labelsFor(root.screenIds, root.screenLabels)
              values: root.labelsFor(root.settingsNow.screens, root.screenLabels)
              onChanged: function(values) {
                var ids = values.map(function(v) { return root.idFor(v, root.screenLabels) })
                  .filter(function(id) { return id !== "" })
                if (ids.length > 0) root.apply({ screens: ids })
              }
            }

            NumberField {
              visible: root.rotating
              label: "Seconds per screen"
              from: 2
              to: 600
              value: root.settingsNow.cycle
              foreground: root.foreground
              fontFamily: root.fontFamily
              onModified: function(v) { root.apply({ cycle: v }) }
            }

            Dropdown {
              width: parent.width
              label: "Bottom area"
              fontFamily: root.fontFamily
              options: root.labelsFor(root.auxIds, root.auxLabels)
              value: root.auxLabels[root.settingsNow.aux] || ""
              onChanged: function(v) { root.apply({ aux: root.idFor(v, root.auxLabels) }) }
            }
          }

          // ---- Picture mode ----
          Column {
            visible: root.settingsNow.mode === "image"
            width: parent.width
            spacing: Style.space(8)

            Row {
              spacing: Style.space(10)
              Button {
                text: "Choose picture…"
                bordered: true
                foreground: root.foreground
                onClicked: pickImageProc.running = true
              }
              Text {
                anchors.verticalCenter: parent.verticalCenter
                text: root.fileName(root.settingsNow.image)
                color: root.foreground
                font.family: root.fontFamily
                font.pixelSize: Style.font.body
                elide: Text.ElideMiddle
                width: column.width - Style.space(160)
              }
            }
            Text {
              width: parent.width
              text: "Scaled and cropped to 480×640. Pictures are stored in the cooler's flash memory, so avoid changing them constantly."
                    + (root.settingsNow.ledColor && !root.settingsNow.image ? " Showing the LED colour border on black." : "")
              color: root.dim
              font.family: root.fontFamily
              font.pixelSize: Style.font.bodySmall
              wrapMode: Text.WordWrap
            }
          }

          PanelSeparator { width: parent.width }

          PanelSectionHeader {
            text: "Brightness  " + (root.settingsNow.brightness > 0 ? root.settingsNow.brightness + "%" : "(screen off)")
            foreground: root.foreground
            fontFamily: root.fontFamily
          }
          PanelSlider {
            width: parent.width
            bar: root.bar
            minimum: 0
            maximum: 100
            step: 1
            integer: true
            value: root.settingsNow.brightness
            onReleased: function(v) { root.apply({ brightness: Math.round(v) }) }
          }

          PanelSectionHeader { text: "LED ring colour"; foreground: root.foreground; fontFamily: root.fontFamily }
          ButtonGroup {
            options: root.labelsFor(["temperature", "motherboard", "picture"], root.ledLabels)
            value: root.ledLabels[root.settingsNow.led] || ""
            foreground: root.foreground
            fontFamily: root.fontFamily
            onChanged: function(v) { root.apply({ led: root.idFor(v, root.ledLabels) }) }
          }
          Text {
            width: parent.width
            text: ({
              "temperature": "Follows the CPU temperature.",
              "motherboard": "Mirrors your motherboard's RGB (set it with your RGB software).",
              "picture": "Takes the colour of the picture's edge. Pick a colour to paint a border in it (re-uploads the picture)."
            })[root.settingsNow.led] || ""
            color: root.dim
            font.family: root.fontFamily
            font.pixelSize: Style.font.bodySmall
            wrapMode: Text.WordWrap
          }
          Flow {
            visible: root.settingsNow.led === "picture"
            width: parent.width
            spacing: Style.space(8)
            Repeater {
              model: root.swatches
              Rectangle {
                required property var modelData
                width: Style.space(24)
                height: width
                radius: Style.cornerRadius > 0 ? width / 2 : 0
                color: modelData
                border.width: root.settingsNow.ledColor.toLowerCase() === modelData ? 3 : 1
                border.color: root.settingsNow.ledColor.toLowerCase() === modelData ? root.foreground : root.dim
                MouseArea {
                  anchors.fill: parent
                  cursorShape: Qt.PointingHandCursor
                  onClicked: root.setLedColor(parent.modelData)
                }
              }
            }
            Button {
              text: "Custom…"
              bordered: true
              foreground: root.foreground
              onClicked: pickColorProc.running = true
            }
            Button {
              text: "No border"
              bordered: true
              foreground: root.foreground
              onClicked: root.apply({ ledColor: "" })
            }
          }

          PanelSectionHeader { text: "When idle"; foreground: root.foreground; fontFamily: root.fontFamily }
          ButtonGroup {
            options: root.labelsFor(["off", "animation"], root.idleLabels)
            value: root.idleLabels[root.settingsNow.idle] || ""
            foreground: root.foreground
            fontFamily: root.fontFamily
            onChanged: function(v) { root.apply({ idle: root.idFor(v, root.idleLabels) }) }
          }

          PanelSectionHeader { text: "Screen orientation"; foreground: root.foreground; fontFamily: root.fontFamily }
          ButtonGroup {
            options: root.rotations
            value: root.rotations[Math.round((root.settingsNow.rotation || 0) / 90) % 4]
            foreground: root.foreground
            fontFamily: root.fontFamily
            onChanged: function(v) { root.apply({ rotation: root.rotations.indexOf(v) * 90 }) }
          }
        }
      }
    }
  }
}
