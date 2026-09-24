# DeepCool LCD — Omarchy bar widget

Shows the CPU temperature in the Omarchy bar; click it to control the MYSTIQUE screen:

- **Stats / Picture** mode
- Built-in **screen** (CPU temp, CPU frequency, pump, CPU fan, fan combo, clock), or **rotate** between several
- **Bottom area**: GHz / CPU % / RAM %, CPU temp / GHz, or 3.3 / 5 / 12 V
- **Picture**: choose any image (scaled/cropped to 480×640). Pictures are stored in the cooler's
  flash, so the service only uploads when the picture actually changes.
- Screen **orientation**

## How it works

The widget never touches USB. The `deepcool-cli` user service owns the cooler, reads
`~/.config/deepcool/control.json` (which the widget writes) every second, and publishes live
readings to `$XDG_RUNTIME_DIR/deepcool/status.json` (which the widget reads).

## Install

```bash
# 1. USB access for your user (once)
sudo cp 70-deepcool.rules /etc/udev/rules.d/ && sudo udevadm control --reload-rules && sudo udevadm trigger

# 2. Build and run the service as your user
cmake -B build -S . && cmake --build build
cmake --install build --prefix ~/.local
cp deepcool.user.service ~/.config/systemd/user/deepcool.service
systemctl --user daemon-reload && systemctl --user enable --now deepcool

# 3. The widget
ln -sfn "$PWD/omarchy-plugin" ~/.config/omarchy/plugins/bulletazz.deepcool
omarchy-shell shell rescanPlugins
omarchy plugin enable bulletazz.deepcool
omarchy bar move bulletazz.deepcool --section center   # icon-only right section clips the text
```

Requires `zenity` for the picture chooser.
