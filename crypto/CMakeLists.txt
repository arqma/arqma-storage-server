add_library(crypto STATIC
    src/arqmad_key.cpp
    src/channel_encryption.cpp
    src/signature.cpp
    ${CMAKE_CURRENT_LIST_DIR}/../vendors/arqma/crypto-ops/keccak.c
    ${CMAKE_CURRENT_LIST_DIR}/../vendors/arqma/crypto-ops/hash-ops.c
    ${CMAKE_CURRENT_LIST_DIR}/../vendors/arqma/crypto-ops/crypto-ops.c
    ${CMAKE_CURRENT_LIST_DIR}/../vendors/arqma/crypto-ops/crypto-ops-data.c)

target_include_directories(crypto
  PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
  PRIVATE
    ${CMAKE_CURRENT_LIST_DIR}/../vendors)

find_package(Threads)

target_link_libraries(crypto
  PUBLIC
    common
    utils
  PRIVATE
    OpenSSL::SSL
    OpenSSL::Crypto
    sodium
    Boost::system
    Boost::program_options
    Boost::filesystem
    Threads::Threads
    ${CMAKE_DL_LIBS})
