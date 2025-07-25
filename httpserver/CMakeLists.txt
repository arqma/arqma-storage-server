add_library(httpserver_lib STATIC
    main.cpp
    http_connection.cpp
    swarm.cpp
    service_node.cpp
    serialization.cpp
    rate_limiter.cpp
    https_client.cpp
    stats.cpp
    security.cpp
    command_line.cpp
    dns_text_records.cpp
    reachability_testing.cpp
    server_certificates.cpp
    )

target_link_libraries(httpserver_lib
  PUBLIC
    common
    storage
    utils
    crypto
    spdlog::spdlog
    OpenSSL::SSL
    OpenSSL::Crypto
    nlohmann_json::nlohmann_json
    Boost::system
    Boost::program_options
    Boost::filesystem)

find_library(RESOLV resolv)
if(RESOLV)
  target_link_libraries(httpserver_lib PUBLIC ${RESOLV})
endif()

target_compile_definitions(httpserver_lib PUBLIC SPDLOG_COMPILED_LIB DISABLE_ENCRYPTION)

set(BIN_NAME arqma-storage)

add_executable(httpserver main.cpp)
set_property(TARGET httpserver PROPERTY RUNTIME_OUTPUT_DIRECTORY "${CMAKE_SOURCE_DIR}/binaries")
set_target_properties(httpserver PROPERTIES OUTPUT_NAME ${BIN_NAME})
target_link_libraries(httpserver PRIVATE httpserver_lib)
install(TARGETS httpserver DESTINATION bin)
# Build Info
find_package(Git)
if(GIT_FOUND)
  execute_process(
    COMMAND
        git rev-parse --short HEAD
    OUTPUT_VARIABLE
        SHORT_HASH
    OUTPUT_STRIP_TRAILING_WHITESPACE)
else()
  set(SHORT_HASH "unknown")
endif()
string(TIMESTAMP BUILD_TIME UTC)
message(STATUS "using git commit hash ${SHORT_HASH}")
message(STATUS "using UTC build time ${BUILD_TIME}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/version.cpp.in" "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")
target_sources(httpserver_lib PRIVATE "${CMAKE_CURRENT_BINARY_DIR}/version.cpp")
target_include_directories(httpserver_lib PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
#
