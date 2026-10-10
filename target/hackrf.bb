#
# HackRF: libhackrf und die Werkzeuge hackrf_info, hackrf_transfer, hackrf_debug usw.
# (nur der Host-Teil des Quelltexts, Verzeichnis host/)
#

SUMMARY = "HackRF: libhackrf und Werkzeuge (hackrf_info, hackrf_transfer, hackrf_debug, ...)"
HOMEPAGE = "https://github.com/greatscottgadgets/hackrf"
SECTION = "PETALINUX/apps"
LICENSE = "GPL-2.0-or-later & BSD-3-Clause"
LIC_FILES_CHKSUM = "file://COPYING;md5=1484b9683e3fc1dd9f5cf802a23fe67c"

PV = "2026.01.3"
SRC_URI = "git://github.com/greatscottgadgets/hackrf.git;protocol=https;nobranch=1"
SRCREV = "1cfe7dfe98d333450217d50e3f3a1ad0702e000f"

S = "${WORKDIR}/git"
OECMAKE_SOURCEPATH = "${S}/host"

# fftw nur fuer hackrf_sweep
DEPENDS = "libusb1 fftw"

inherit cmake pkgconfig

# udev-Regeln sind unnoetig, auf dem Board laeuft alles als root
EXTRA_OECMAKE = "-DRELEASE=${PV} -DINSTALL_UDEV_RULES=OFF"
