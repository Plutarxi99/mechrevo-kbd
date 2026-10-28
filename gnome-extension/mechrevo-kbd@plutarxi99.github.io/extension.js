// SPDX-License-Identifier: GPL-2.0
// Quick Settings tile for mechrevo_kbd: color presets and effects, driven through kbd-color.

import Gio from 'gi://Gio';
import GLib from 'gi://GLib';
import GObject from 'gi://GObject';
import St from 'gi://St';

import {Extension} from 'resource:///org/gnome/shell/extensions/extension.js';
import * as Main from 'resource:///org/gnome/shell/ui/main.js';
import * as PopupMenu from 'resource:///org/gnome/shell/ui/popupMenu.js';
import {QuickMenuToggle, SystemIndicator} from 'resource:///org/gnome/shell/ui/quickSettings.js';

const APP_ID = 'io.github.plutarxi99.MechrevoKbd.desktop';
const CONFIG_DIR = GLib.build_filenamev([GLib.get_user_config_dir(), 'kbd-color']);
const EFFECT_FILE = GLib.build_filenamev([CONFIG_DIR, 'effect']);

// Same names and colors as kbd-color and the app
const PRESETS = [
    ['red', [255, 0, 0]], ['orange', [255, 80, 0]], ['yellow', [255, 255, 0]],
    ['green', [0, 255, 0]], ['cyan', [0, 255, 255]], ['blue', [0, 0, 255]],
    ['purple', [128, 0, 255]], ['magenta', [255, 0, 255]], ['white', [255, 255, 255]],
];
const EFFECTS = [
    ['static', 'Static'], ['breathing', 'Breathing'], ['blink', 'Blink'],
    ['cycle', 'Color cycle'], ['rainbow', 'Rainbow'],
];

const RU = {
    'Keyboard Color': 'Цвет клавиатуры',
    'Static': 'Постоянный', 'Breathing': 'Дыхание', 'Blink': 'Мигание',
    'Color cycle': 'Переливание', 'Rainbow': 'Радуга',
    'Settings…': 'Настройки…',
};
const isRu = GLib.get_language_names().some(l => l.startsWith('ru'));
const _ = s => (isRu && RU[s]) || s;

function findKbdColor() {
    return GLib.find_program_in_path('kbd-color') ??
        GLib.build_filenamev([GLib.get_home_dir(), '.local', 'bin', 'kbd-color']);
}

function ledPresent() {
    const dir = Gio.File.new_for_path('/sys/class/leds');
    try {
        const iter = dir.enumerate_children('standard::name', Gio.FileQueryInfoFlags.NONE, null);
        for (let info; (info = iter.next_file(null));) {
            if (info.get_name().endsWith(':rgb:kbd_backlight'))
                return true;
        }
    } catch {
        // No LED class at all
    }
    return false;
}

function readEffect() {
    try {
        const [, bytes] = GLib.file_get_contents(EFFECT_FILE);
        const [mode] = new TextDecoder().decode(bytes).trim().split(/\s+/);
        if (EFFECTS.some(([id]) => id === mode))
            return mode;
    } catch {
        // No effect chosen yet
    }
    return 'static';
}

const KeyboardColorToggle = GObject.registerClass(
class KeyboardColorToggle extends QuickMenuToggle {
    _init() {
        super._init({
            title: _('Keyboard Color'),
            iconName: 'preferences-color-symbolic',
            toggleMode: false,
        });
        this._kbdColor = findKbdColor();
        this._lastEffect = 'breathing';

        this.menu.setHeader('preferences-color-symbolic', _('Keyboard Color'));

        const swatchItem = new PopupMenu.PopupBaseMenuItem({reactive: false, can_focus: false});
        const swatches = new St.BoxLayout({style_class: 'mechrevo-kbd-swatches', x_expand: true});
        for (const [name, [r, g, b]] of PRESETS) {
            const button = new St.Button({
                style_class: 'mechrevo-kbd-swatch',
                style: `background-color: rgb(${r},${g},${b});`,
                accessible_name: name,
                can_focus: true,
            });
            button.connect('clicked', () => this._run([`${r}`, `${g}`, `${b}`]));
            swatches.add_child(button);
        }
        swatchItem.add_child(swatches);
        this.menu.addMenuItem(swatchItem);

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this._effectItems = new Map();
        for (const [id, label] of EFFECTS) {
            const item = new PopupMenu.PopupMenuItem(_(label));
            item.connect('activate', () => this._setEffect(id));
            this.menu.addMenuItem(item);
            this._effectItems.set(id, item);
        }

        this.menu.addMenuItem(new PopupMenu.PopupSeparatorMenuItem());
        this.menu.addAction(_('Settings…'), () => {
            Gio.DesktopAppInfo.new(APP_ID)?.launch([], null);
            Main.panel.closeQuickSettings();
        });

        // Clicking the tile itself turns the last used effect on and off
        this.connect('clicked', () =>
            this._setEffect(this._effect === 'static' ? this._lastEffect : 'static'));

        // Follow changes made from the app or the command line
        this._monitor = Gio.File.new_for_path(EFFECT_FILE)
            .monitor_file(Gio.FileMonitorFlags.NONE, null);
        this._monitor.connect('changed', () => this._sync());
        this._sync();
    }

    _sync() {
        this._effect = readEffect();
        if (this._effect !== 'static')
            this._lastEffect = this._effect;
        this.checked = this._effect !== 'static';
        this.subtitle = this.checked ? _(EFFECTS.find(([id]) => id === this._effect)[1]) : null;
        for (const [id, item] of this._effectItems) {
            item.setOrnament(id === this._effect
                ? PopupMenu.Ornament.CHECK : PopupMenu.Ornament.NONE);
        }
    }

    _setEffect(id) {
        this._run(['effect', id]);
    }

    _run(args) {
        try {
            Gio.Subprocess.new([this._kbdColor, ...args], Gio.SubprocessFlags.NONE);
        } catch (e) {
            console.error(`mechrevo-kbd: cannot run ${this._kbdColor}: ${e.message}`);
        }
    }

    destroy() {
        this._monitor?.cancel();
        this._monitor = null;
        super.destroy();
    }
});

const KeyboardColorIndicator = GObject.registerClass(
class KeyboardColorIndicator extends SystemIndicator {
    _init() {
        super._init();
        this.quickSettingsItems.push(new KeyboardColorToggle());
    }

    destroy() {
        this.quickSettingsItems.forEach(item => item.destroy());
        super.destroy();
    }
});

export default class MechrevoKbdExtension extends Extension {
    enable() {
        if (!ledPresent())
            return;
        this._indicator = new KeyboardColorIndicator();
        Main.panel.statusArea.quickSettings.addExternalIndicator(this._indicator);
    }

    disable() {
        this._indicator?.destroy();
        this._indicator = null;
    }
}
