obj-m := mechrevo_kbd.o

KVER ?= $(shell uname -r)
KDIR ?= /lib/modules/$(KVER)/build
KEYDIR ?= $(CURDIR)/keys

all:
	$(MAKE) -C $(KDIR) M=$(CURDIR) modules

clean:
	$(MAKE) -C $(KDIR) M=$(CURDIR) clean

# Self-made Machine Owner Key: enroll once with `make enroll`, then reboot
keys:
	mkdir -p $(KEYDIR)
	openssl req -new -x509 -newkey rsa:2048 -nodes -days 36500 -outform DER \
		-subj "/CN=mechrevo-kbd module signing/" \
		-keyout $(KEYDIR)/MOK.priv -out $(KEYDIR)/MOK.der
	chmod 600 $(KEYDIR)/MOK.priv

enroll:
	sudo mokutil --import $(KEYDIR)/MOK.der

sign: all
	$(KDIR)/scripts/sign-file sha256 $(KEYDIR)/MOK.priv $(KEYDIR)/MOK.der mechrevo_kbd.ko

.PHONY: all clean keys enroll sign
