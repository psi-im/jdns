cmake_minimum_required(VERSION 3.19)

set(jdns_tool_MOC_HDRS
    main.h
)

set(jdns_tool_SRCS
    main.cpp
)

add_executable(jdns-tool
    ${jdns_tool_SRCS}
    ${jdns_tool_MOC_HDRS}
)

target_link_libraries(jdns-tool PRIVATE QJDns::QJDns)

set_target_properties(jdns-tool PROPERTIES OUTPUT_NAME jdns)

install(TARGETS jdns-tool
    RUNTIME DESTINATION "${BIN_INSTALL_DIR}"
)

if(MSVC)
    install(FILES "$<TARGET_PDB_FILE:jdns-tool>"
        DESTINATION "${BIN_INSTALL_DIR}"
        CONFIGURATIONS Debug RelWithDebInfo
        OPTIONAL
    )
endif()
