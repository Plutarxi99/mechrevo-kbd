---
name: Device report
about: Tell us whether mechrevo-kbd works on your laptop
title: "[device] "
labels: device-report
---

**Laptop model** (e.g. MECHREVO WUJIE15XA):

**Output of:**

```sh
cat /sys/class/dmi/id/sys_vendor /sys/class/dmi/id/product_name /sys/class/dmi/id/board_name
uname -r
sudo dmesg | grep mechrevo
```

```
paste here
```

**What works?**

- [ ] module loads
- [ ] color changes
- [ ] brightness changes
- [ ] hardware rainbow (`kbd-color effect rainbow`)
- [ ] breathing / blink / color cycle

**Installed with** `--force`? yes / no

**Anything else:**
