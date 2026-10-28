#!/usr/bin/env bash
# Remove everything install.sh put in place. Saved colors in ~/.config/kbd-color are kept.
set -euo pipefail

NAME=mechrevo-kbd
PREFIX=/usr/local

[[ $EUID -eq 0 ]] || { echo "run as root: sudo $0" >&2; exit 1; }

systemctl --global disable kbd-color-restore.service 2> /dev/null || true
rm -f /etc/systemd/user/kbd-color-restore.service /etc/systemd/user/mechrevo-kbd-effects.service
rm -f "$PREFIX/bin/mechrevo-kbd-gui" "$PREFIX/bin/kbd-color" "$PREFIX/bin/mechrevo-kbd-effects" \
	"$PREFIX/share/applications/io.github.plutarxi99.MechrevoKbd.desktop"
rm -f /etc/udev/rules.d/90-mechrevo-kbd.rules /etc/modprobe.d/$NAME.conf /etc/modules-load.d/$NAME.conf
udevadm control --reload

modprobe -r mechrevo_kbd 2> /dev/null || true
for v in $(dkms status -m "$NAME" 2> /dev/null | sed -n 's|^[^/]*/\([^,:]*\).*|\1|p' | sort -u); do
	dkms remove -m "$NAME" -v "$v" --all
	rm -rf "/usr/src/$NAME-$v"
done

echo "mechrevo-kbd removed. Users who turned on an effect can run: systemctl --user disable mechrevo-kbd-effects"
