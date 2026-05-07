SUMMARY = "Linux Test Runner"
DESCRIPTION = "Tools to record, send, receive and execute emulated inputs from remote sources"
AUTHOR = "matt.everett@toyotaconnected.com"
HOMEPAGE = "https://github.com/toyota-connected/test_runner"
BUGTRACKER = "https://github.com/toyota-connected/test_runner/issues"
LICENSE = "GPLv3"
LIC_FILES_CHKSUM = "1ebbd3e34237af26da5dc08a4e440464"

DEPENDS += "\
    libinput \
    wayland \
    capnproto \
    capnproto-native \
    "

SRCREV = "${AUTOREV}"
SRC_URI = "git://github.com/toyota-connected/test_runner.git;protocol=https;branch=main"

S = "${WORKDIR}/git"

inherit cmake pkgconfig 

EXTRA_OECMAKE += "\
    -D BUILD_SERVER=ON \
    -D BUILD_RECORDER=ON \
    -D BUILD_EXAMPLES=ON \
    -D BUILD_TESTING=OFF \
    -D BUILD_CAPNP=OFF \
"

SOLIBS = ".so"

FILES:${PN} += "${libdir}/* ${bindir}/*"
FILES:${PN}-dev = " \
    ${includedir} \
"

SYSROOT_DIRS:append = " ${bindir}"

BBCLASSEXTEND += "native nativesdk"
