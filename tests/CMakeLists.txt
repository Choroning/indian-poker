add_executable(ip_tests
    test_main.cpp
    test_sha1_base64.cpp
    test_ws_handshake.cpp
    test_ws_frame.cpp
)
target_link_libraries(ip_tests PRIVATE ip_core Catch2::Catch2WithMain)
target_include_directories(ip_tests PRIVATE ${CMAKE_SOURCE_DIR})

include(Catch)
catch_discover_tests(ip_tests)
