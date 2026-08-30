cmake_minimum_required(VERSION 3.19)

set(qjdns_MOC_HDRS
    "${JDNS_INCLUDEDIR}/qjdns.h"
    "${JDNS_INCLUDEDIR}/qjdnsshared.h"
    qjdns_p.h
    qjdnsshared_p.h
)

set(qjdns_SRCS
    qjdns.cpp
    qjdns_sock.cpp
    qjdnsshared.cpp
)

set(qjdns_PUBLIC_HEADERS
    "${JDNS_INCLUDEDIR}/qjdns.h"
    "${JDNS_INCLUDEDIR}/qjdnsshared.h"
)

set(qjdns_HEADERS
    qjdns_sock.h
)

add_library(qjdns
    ${qjdns_SRCS}
    ${qjdns_MOC_HDRS}
    ${qjdns_HEADERS}
    ${qjdns_PUBLIC_HEADERS}
)
add_library(QJDns::QJDns ALIAS qjdns)

if(QT_MAJ)
    set_target_properties(qjdns PROPERTIES OUTPUT_NAME "qjdns${QT_MAJ}")
endif()

target_include_directories(qjdns
    PUBLIC
        $<BUILD_INTERFACE:${PROJECT_SOURCE_DIR}/include/jdns>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/jdns>
)

target_link_libraries(qjdns
    PUBLIC
        JDns::JDns
        ${QJDNS_QT_TARGETS}
)

if(NOT ANDROID)
    set_target_properties(qjdns PROPERTIES
        VERSION "${JDNS_LIB_VERSION_STRING}"
        SOVERSION "${JDNS_LIB_MAJOR_VERSION}"
    )
endif()

set_target_properties(qjdns PROPERTIES
    DEFINE_SYMBOL JDNS_MAKEDLL
    PUBLIC_HEADER "${qjdns_PUBLIC_HEADERS}"
)

install(TARGETS qjdns
    EXPORT qjdns-export
    LIBRARY DESTINATION "${LIB_INSTALL_DIR}"
    RUNTIME DESTINATION "${BIN_INSTALL_DIR}"
    ARCHIVE DESTINATION "${LIB_INSTALL_DIR}"
    PUBLIC_HEADER DESTINATION "${INCLUDE_INSTALL_DIR}/jdns"
    INCLUDES DESTINATION "${INCLUDE_INSTALL_DIR}/jdns"
)

if(MSVC AND BUILD_SHARED_LIBS)
    install(FILES "$<TARGET_PDB_FILE:qjdns>"
        DESTINATION "${BIN_INSTALL_DIR}"
        CONFIGURATIONS Debug RelWithDebInfo
        OPTIONAL
    )
endif()
