add_library(crypto STATIC
    src/arqmad_key.cpp
    src/channel_encryption.cpp
    src/signature.cpp
)

target_include_directories(crypto PUBLIC include)

find_package(Threads)

target_link_libraries(crypto
  PUBLIC
    common
    utils
  PRIVATE
    arqma-crypto-ops
    OpenSSL::SSL
    OpenSSL::Crypto
    sodium
    Boost::system
    Boost::program_options
    Boost::filesystem
    Threads::Threads
    ${CMAKE_DL_LIBS})
