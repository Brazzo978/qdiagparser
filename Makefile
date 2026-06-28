PYTHON ?= python3
CC_ARM ?= arm-linux-gnueabihf-gcc
CFLAGS ?= -O2 -Wall -Wextra -std=c11
MODEM_SYSROOT ?= /home/manu/Scrivania/qcmap/backups/20260407-174905-qcmap-192.168.225.1/rootfs
DOCKER_IMAGE ?= debian:10

.PHONY: test arm dci dci-docker clean

test:
	PYTHONPATH=src $(PYTHON) -m unittest discover -s tests -v

arm:
	mkdir -p build
	$(CC_ARM) $(CFLAGS) -static -o build/qdiagmon-armhf csrc/qdiagmon.c

dci:
	mkdir -p build
	$(CC_ARM) $(CFLAGS) "--sysroot=$(MODEM_SYSROOT)" -Wl,--allow-shlib-undefined "-Wl,-rpath-link,$(MODEM_SYSROOT)/lib" "-Wl,-rpath-link,$(MODEM_SYSROOT)/usr/lib" "-Wl,-rpath-link,$(MODEM_SYSROOT)/opt/lib" "-L$(MODEM_SYSROOT)/usr/lib" -o build/qdiagmon-dci-armhf csrc/qdiagmon_dci.c -l:libdiag.so.1 -l:liblog.so.0

dci-docker:
	docker run --rm --platform linux/amd64 -v "$(CURDIR):/work" -v "$(MODEM_SYSROOT):/sysroot:ro" -w /work $(DOCKER_IMAGE) bash -lc 'printf "%s\n" "deb http://archive.debian.org/debian buster main" "deb http://archive.debian.org/debian-security buster/updates main" > /etc/apt/sources.list && apt-get -o Acquire::Check-Valid-Until=false update >/dev/null && apt-get install -y gcc-arm-linux-gnueabihf binutils-arm-linux-gnueabihf make >/dev/null && mkdir -p build && arm-linux-gnueabihf-gcc -O2 -Wall -Wextra -std=c11 --sysroot=/sysroot -Wl,--allow-shlib-undefined -Wl,-rpath-link,/sysroot/lib -Wl,-rpath-link,/sysroot/usr/lib -L/sysroot/usr/lib -o build/qdiagmon-dci-armhf csrc/qdiagmon_dci.c -l:libdiag.so.1 -l:liblog.so.0'

clean:
	rm -rf build src/qdiagparser/__pycache__ tests/__pycache__
