#
# Werkzeuge fuer das ZedBoard: Sinuston (Codec-Init, Frequenz), HDMI-Testbild,
# Testprogramm fuer die Framebuffer, Empfangstest fuer das HackRF und Messung der
# Interrupt-Laufzeit, Text auf der OLED-Anzeige, bewegte Linien fuer Testbild 8; dazu der
# SSH-Zugang fuer board.sh (fester Host-Schluessel, Schluessel des PCs fuer root)
#

SUMMARY = "ZedBoard-Werkzeuge: codec_init.sh, set_freq.sh, hdmi_init.sh, set_pattern.sh, set_scope.sh, fb_test, iq_rx, irq_test, oled, vec_demo"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://codec_init.sh \
	   file://set_freq.sh \
	   file://hdmi_init.sh \
	   file://set_pattern.sh \
	   file://set_scope.sh \
	   file://fb_test.c \
	   file://iq_rx.c \
	   file://irq_test.c \
	   file://oled.c \
	   file://oled.h \
	   file://oled_font.h \
	   file://vec_demo.c \
	   file://lines.h \
	   file://uio.conf \
	   file://authorized_keys \
	   file://ssh_host_ed25519_key \
	   file://ssh_host_ed25519_key.pub \
	"

S = "${WORKDIR}"

# hackrf: libhackrf fuer iq_rx, zur Laufzeit auch hackrf_info, hackrf_transfer usw.
DEPENDS = "hackrf"
RDEPENDS:${PN} = "i2c-tools hackrf"

do_compile() {
	     ${CC} ${CFLAGS} ${LDFLAGS} -o fb_test ${S}/fb_test.c -lm
	     ${CC} ${CFLAGS} ${LDFLAGS} -o iq_rx ${S}/iq_rx.c -lhackrf -lm -lpthread
	     ${CC} ${CFLAGS} ${LDFLAGS} -o irq_test ${S}/irq_test.c -lm
	     ${CC} ${CFLAGS} ${LDFLAGS} -o oled ${S}/oled.c
	     ${CC} ${CFLAGS} ${LDFLAGS} -o vec_demo ${S}/vec_demo.c -lm
}

do_install() {
	     install -d ${D}/${bindir}
	     install -m 0755 ${S}/codec_init.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_freq.sh ${D}/${bindir}
	     install -m 0755 ${S}/hdmi_init.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_pattern.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_scope.sh ${D}/${bindir}
	     install -m 0755 fb_test ${D}/${bindir}
	     install -m 0755 iq_rx ${D}/${bindir}
	     install -m 0755 irq_test ${D}/${bindir}
	     install -m 0755 oled ${D}/${bindir}
	     install -m 0755 vec_demo ${D}/${bindir}

	     install -d ${D}/${sysconfdir}/modprobe.d
	     install -m 0644 ${S}/uio.conf ${D}/${sysconfdir}/modprobe.d

	     install -d -m 0700 ${D}/home/root/.ssh
	     install -m 0600 ${S}/authorized_keys ${D}/home/root/.ssh
	     install -d ${D}/${sysconfdir}/ssh
	     install -m 0600 ${S}/ssh_host_ed25519_key ${D}/${sysconfdir}/ssh
	     install -m 0644 ${S}/ssh_host_ed25519_key.pub ${D}/${sysconfdir}/ssh
}

FILES:${PN} += "/home/root/.ssh ${sysconfdir}/ssh ${sysconfdir}/modprobe.d"
