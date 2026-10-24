cmake_minimum_required(VERSION 3.19)
find_package(Qt${JDNS_QT_MAJOR_VERSION} REQUIRED COMPONENTS Test)

add_executable(qjdns-remote-transport-test remotetransport.cpp)
set_target_properties(qjdns-remote-transport-test PROPERTIES AUTOMOC ON)
target_link_libraries(qjdns-remote-transport-test PRIVATE
    QJDns::QJDns
    Qt${JDNS_QT_MAJOR_VERSION}::Test
    Qt${JDNS_QT_MAJOR_VERSION}::Network
)
add_test(NAME qjdns-remote-transport COMMAND qjdns-remote-transport-test)
