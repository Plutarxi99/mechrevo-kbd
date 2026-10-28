#!/usr/bin/env bash
# Install mechrevo-kbd: DKMS kernel module, udev rule, GUI, CLI and the restore service.
#   sudo ./install.sh           MECHREVO laptops
#   sudo ./install.sh --force   other Uniwill-based laptops (module loads with force=1)
set -euo pipefail

NAME=mechrevo-kbd
VERSION=$(sed -n 's/^PACKAGE_VERSION="\(.*\)"/\1/p' "$(dirname "$0")/dkms.conf")
SRC=$(cd "$(dirname "$0")" && pwd)
PREFIX=/usr/local
FORCE=0
[[ ${1:-} == --force ]] && FORCE=1

die() { echo "error: $*" >&2; exit 1; }
step() { echo "==> $*"; }

[[ $EUID -eq 0 ]] || die "run as root: sudo $0 $*"
command -v dkms > /dev/null || die "dkms is not installed.
  Debian/Ubuntu: sudo apt install dkms linux-headers-\$(uname -r)
  Fedora:        sudo dnf install dkms kernel-devel
  Arch:          sudo pacman -S dkms linux-headers"
[[ -d /lib/modules/$(uname -r)/build ]] || die "kernel headers for $(uname -r) are missing"
python3 -c 'import gi; gi.require_version("Gtk", "4.0"); gi.require_version("Adw", "1")' 2> /dev/null ||
	echo "warning: GTK4/libadwaita for Python not found, the GUI will not start
  Debian/Ubuntu: sudo apt install python3-gi gir1.2-gtk-4.0 gir1.2-adw-1" >&2

step "kernel module via DKMS ($NAME $VERSION)"
if dkms status -m "$NAME" -v "$VERSION" 2> /dev/null | grep -q .; then
	dkms remove -m "$NAME" -v "$VERSION" --all > /dev/null
fi
rm -rf "/usr/src/$NAME-$VERSION"
install -d "/usr/src/$NAME-$VERSION"
install -m 644 "$SRC/mechrevo_kbd.c" "$SRC/Makefile" "$SRC/dkms.conf" "/usr/src/$NAME-$VERSION/"
dkms install -m "$NAME" -v "$VERSION"

if (( FORCE )); then
	step "force=1 for a non-MECHREVO laptop"
	echo "options mechrevo_kbd force=1" > /etc/modprobe.d/$NAME.conf
	echo mechrevo_kbd > /etc/modules-load.d/$NAME.conf
fi

step "udev rule"
install -m 644 "$SRC/data/90-mechrevo-kbd.rules" /etc/udev/rules.d/
udevadm control --reload

step "GUI, CLI and desktop entry"
install -m 755 "$SRC/gui/mechrevo-kbd-gui" "$PREFIX/bin/mechrevo-kbd-gui"
install -m 755 "$SRC/kbd-color" "$PREFIX/bin/kbd-color"
install -m 755 "$SRC/mechrevo-kbd-effects" "$PREFIX/bin/mechrevo-kbd-effects"
install -Dm 644 "$SRC/data/io.github.plutarxi99.MechrevoKbd.desktop" \
	"$PREFIX/share/applications/io.github.plutarxi99.MechrevoKbd.desktop"

step "user services (color restore for everyone, effects on demand)"
install -Dm 644 "$SRC/data/kbd-color-restore.service" /etc/systemd/user/kbd-color-restore.service
install -Dm 644 "$SRC/data/mechrevo-kbd-effects.service" /etc/systemd/user/mechrevo-kbd-effects.service
systemctl --global enable kbd-color-restore.service

step "loading the module"
if modprobe mechrevo_kbd 2> /dev/null; then
	udevadm trigger --subsystem-match=leds --action=add
	udevadm trigger --subsystem-match=platform --sysname-match=mechrevo_kbd --action=add
	echo
	echo "Done. Open \"Keyboard Backlight\" from the app menu or run: kbd-color red"
	exit 0
fi

echo
if mokutil --sb-state 2> /dev/null | grep -q enabled; then
	key=/var/lib/shim-signed/mok/MOK.der
	echo "Secure Boot is on and the module could not be loaded."
	echo "DKMS signed it with $key, which the firmware does not trust yet. Enroll it once:"
	echo "  sudo mokutil --import $key"
	echo "then reboot, choose \"Enroll MOK\" on the blue screen and enter the password you set."
else
	echo "The module did not load. See: sudo dmesg | grep mechrevo"
	(( FORCE )) || echo "Not a MECHREVO laptop? Try: sudo $0 --force"
fi
exit 1
