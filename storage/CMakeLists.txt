add_library(storage STATIC
  src/Database.cpp)

target_include_directories(storage
    PUBLIC
    ${CMAKE_CURRENT_LIST_DIR}/include
)

target_link_libraries(storage
  PRIVATE
    common
    utils
    sqlite3
    Boost::boost)
