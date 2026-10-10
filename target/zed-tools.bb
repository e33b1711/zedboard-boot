#
# Werkzeuge fuer das ZedBoard: Sinuston (Codec-Init, Frequenz), HDMI-Testbild
# und Testprogramm fuer die Framebuffer; dazu der SSH-Zugang fuer board.sh
# (fester Host-Schluessel, Schluessel des PCs fuer root)
#

SUMMARY = "ZedBoard-Werkzeuge: codec_init.sh, set_freq.sh, hdmi_init.sh, set_pattern.sh, fb_test"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://codec_init.sh \
	   file://set_freq.sh \
	   file://hdmi_init.sh \
	   file://set_pattern.sh \
	   file://fb_test.c \
	   file://authorized_keys \
	   file://ssh_host_ed25519_key \
	   file://ssh_host_ed25519_key.pub \
	"

S = "${WORKDIR}"

RDEPENDS:${PN} = "i2c-tools"

do_compile() {
	     ${CC} ${CFLAGS} ${LDFLAGS} -o fb_test ${S}/fb_test.c -lm
}

do_install() {
	     install -d ${D}/${bindir}
	     install -m 0755 ${S}/codec_init.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_freq.sh ${D}/${bindir}
	     install -m 0755 ${S}/hdmi_init.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_pattern.sh ${D}/${bindir}
	     install -m 0755 fb_test ${D}/${bindir}

	     install -d -m 0700 ${D}/home/root/.ssh
	     install -m 0600 ${S}/authorized_keys ${D}/home/root/.ssh
	     install -d ${D}/${sysconfdir}/ssh
	     install -m 0600 ${S}/ssh_host_ed25519_key ${D}/${sysconfdir}/ssh
	     install -m 0644 ${S}/ssh_host_ed25519_key.pub ${D}/${sysconfdir}/ssh
}

FILES:${PN} += "/home/root/.ssh ${sysconfdir}/ssh"
