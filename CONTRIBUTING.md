# Contributing

Thanks for wanting to help! [Русская версия ниже](#как-помочь-проекту).

## The most useful thing: test on your laptop

Only one model is confirmed so far. If you have a MECHREVO or another Uniwill-based laptop,
install the project and open a
[device report](https://github.com/Plutarxi99/mechrevo-kbd/issues/new?template=device-report.yml),
even if nothing works. That tells us which models to support and which EC registers differ.

## Other ways to help

- **Bugs** → [bug report](https://github.com/Plutarxi99/mechrevo-kbd/issues/new?template=bug-report.yml)
- **Ideas** → [idea](https://github.com/Plutarxi99/mechrevo-kbd/issues/new?template=feature-request.yml),
  or start a [discussion](https://github.com/Plutarxi99/mechrevo-kbd/discussions) first for bigger changes
- **Code** → issues labelled
  [`good first issue`](https://github.com/Plutarxi99/mechrevo-kbd/labels/good%20first%20issue) and
  [`help wanted`](https://github.com/Plutarxi99/mechrevo-kbd/labels/help%20wanted)
- **Translations, packaging** (AUR, .deb, Fedora COPR), docs, screenshots

## Pull requests

1. Fork the repo and create a branch: `git checkout -b fix-something`.
2. Make the change; keep one topic per PR.
3. Open a PR to `main`. CI builds the module, runs `checkpatch` and checks the scripts.
4. The maintainer reviews and merges. Only the maintainer pushes to `main` directly.

For anything bigger than a small fix, open an issue first, so we agree on the approach before
you spend time on it.

## Project layout

| Path | What |
|---|---|
| `mechrevo_kbd.c` | kernel module: EC access, multicolor LED, `rainbow` switch |
| `gui/mechrevo-kbd-gui` | GTK4/libadwaita app (Python) |
| `mechrevo-kbd-effects` | user service that draws breathing / blink / cycle |
| `kbd-color` | command-line tool |
| `data/` | udev rule, systemd user units, desktop entry |
| `install.sh`, `uninstall.sh`, `dkms.conf` | installation |

## Development setup

```sh
sudo apt install build-essential linux-headers-$(uname -r) python3-gi gir1.2-gtk-4.0 gir1.2-adw-1
make                                     # build mechrevo_kbd.ko
sudo rmmod mechrevo_kbd; sudo insmod ./mechrevo_kbd.ko   # try it
sudo dmesg | grep mechrevo
./gui/mechrevo-kbd-gui                   # run the app from the checkout
```

**Secure Boot:** a self-built module has to be signed. Create your own key once and enroll it:

```sh
make keys            # keys/MOK.priv + keys/MOK.der (git-ignored, never commit them)
make enroll          # mokutil --import, then reboot and "Enroll MOK"
make sign            # build and sign; run after every change
```

## Code style

- **Kernel module:** Linux kernel style; `scripts/checkpatch.pl -f --no-tree --strict mechrevo_kbd.c`
  must be clean. Any new EC register needs a source (mainline `uniwill-laptop`, `tuxedo-drivers`,
  or a careful description of how you found it) — wrong EC writes can affect fans or charging.
- **Python:** standard library + PyGObject only, no extra dependencies.
- **Shell:** `bash -n` and `shellcheck -S warning` clean.
- **Commits:** short imperative subject (`Add per-key …`, `Fix …`), body explains why.

By contributing you agree that your code is released under the project's
[GPL-2.0 license](LICENSE), and to follow the [Code of Conduct](CODE_OF_CONDUCT.md).

---

## Как помочь проекту

**Самое полезное — проверить на своём ноутбуке.** Пока подтверждена одна модель. Если у вас
MECHREVO или другой ноутбук на базе Uniwill, установите проект и заполните
[отчёт о модели](https://github.com/Plutarxi99/mechrevo-kbd/issues/new?template=device-report.yml),
даже если ничего не заработало.

**Другие способы помочь:**

- ошибки — [отчёт об ошибке](https://github.com/Plutarxi99/mechrevo-kbd/issues/new?template=bug-report.yml);
- идеи — [предложение](https://github.com/Plutarxi99/mechrevo-kbd/issues/new?template=feature-request.yml)
  или сначала [обсуждение](https://github.com/Plutarxi99/mechrevo-kbd/discussions);
- код — задачи с метками `good first issue` и `help wanted`;
- переводы, пакеты (AUR, .deb, COPR), документация.

**Pull request:** сделайте fork, создайте ветку, внесите изменение (одна тема на PR) и откройте PR
в `main`. CI соберёт модуль и проверит стиль. Сливает изменения мейнтейнер; напрямую в `main`
пушит только он. Для крупных изменений сначала откройте issue, чтобы договориться о подходе.

Сборка, подпись для Secure Boot и стиль кода описаны выше, в английской части.
Участвуя в проекте, вы соглашаетесь, что ваш код распространяется под лицензией GPL-2.0,
и соблюдаете [кодекс поведения](CODE_OF_CONDUCT.md): уважительное общение, без оскорблений и травли.
