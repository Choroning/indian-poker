add_executable(ip_tests
    test_main.cpp
    # files added per milestone
)
target_link_libraries(ip_tests PRIVATE ip_core Catch2::Catch2WithMain)
target_include_directories(ip_tests PRIVATE ${CMAKE_SOURCE_DIR})

include(Catch)
catch_discover_tests(ip_tests)
