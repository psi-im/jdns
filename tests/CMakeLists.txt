cmake_minimum_required(VERSION 3.19)
find_package(Qt${JDNS_QT_MAJOR_VERSION} REQUIRED COMPONENTS Test)

add_executable(qjdns-transport-test transports.cpp)
set_target_properties(qjdns-transport-test PROPERTIES AUTOMOC ON)
target_link_libraries(qjdns-transport-test PRIVATE
    QJDns::QJDns
    Qt${JDNS_QT_MAJOR_VERSION}::Test
    Qt${JDNS_QT_MAJOR_VERSION}::Network
)
add_test(NAME qjdns-transports COMMAND qjdns-transport-test)
