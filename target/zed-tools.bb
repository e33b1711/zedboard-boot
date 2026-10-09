#
# Skripte fuer das ZedBoard: Sinuston (Codec-Init, Frequenz) und HDMI-Testbild
#

SUMMARY = "ZedBoard-Skripte: codec_init.sh, set_freq.sh, hdmi_init.sh, set_pattern.sh"
SECTION = "PETALINUX/apps"
LICENSE = "MIT"
LIC_FILES_CHKSUM = "file://${COMMON_LICENSE_DIR}/MIT;md5=0835ade698e0bcf8506ecda2f7b4f302"

SRC_URI = "file://codec_init.sh \
	   file://set_freq.sh \
	   file://hdmi_init.sh \
	   file://set_pattern.sh \
	"

S = "${WORKDIR}"

RDEPENDS:${PN} = "i2c-tools"

do_install() {
	     install -d ${D}/${bindir}
	     install -m 0755 ${S}/codec_init.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_freq.sh ${D}/${bindir}
	     install -m 0755 ${S}/hdmi_init.sh ${D}/${bindir}
	     install -m 0755 ${S}/set_pattern.sh ${D}/${bindir}
}
