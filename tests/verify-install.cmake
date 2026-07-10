foreach(variable INSTALL_ROOT INSTALL_BINDIR INSTALL_SYSCONFDIR INSTALL_DATADIR)
    if(NOT DEFINED ${variable})
        message(FATAL_ERROR "${variable} must be set")
    endif()
endforeach()

set(required_files
    "${INSTALL_ROOT}${INSTALL_BINDIR}/custom-toolbox"
    "${INSTALL_ROOT}${INSTALL_SYSCONFDIR}/custom-toolbox/custom-toolbox.conf"
    "${INSTALL_ROOT}${INSTALL_SYSCONFDIR}/custom-toolbox/example.list"
    "${INSTALL_ROOT}${INSTALL_DATADIR}/applications/custom-toolbox.desktop"
    "${INSTALL_ROOT}${INSTALL_DATADIR}/pixmaps/custom-toolbox.svg"
    "${INSTALL_ROOT}${INSTALL_DATADIR}/icons/hicolor/scalable/apps/custom-toolbox.svg"
    "${INSTALL_ROOT}${INSTALL_DATADIR}/doc/custom-toolbox/help.html"
    "${INSTALL_ROOT}${INSTALL_DATADIR}/doc/custom-toolbox/license.html"
    "${INSTALL_ROOT}${INSTALL_DATADIR}/doc/custom-toolbox/changelog"
)

foreach(path IN LISTS required_files)
    if(NOT EXISTS "${path}")
        message(FATAL_ERROR "Missing installed runtime file: ${path}")
    endif()
endforeach()

file(GLOB translations "${INSTALL_ROOT}${INSTALL_DATADIR}/custom-toolbox/locale/*.qm")
if(NOT translations)
    message(FATAL_ERROR "No compiled translations were installed")
endif()
