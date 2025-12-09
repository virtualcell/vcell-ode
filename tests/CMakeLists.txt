#############################################
#  GoogleTest
##############################################
include(FetchContent)
FetchContent_Declare(
        googletest
        GIT_REPOSITORY https://github.com/google/googletest.git
        GIT_TAG          v1.17.0
)
if (WIN32)
    # For Windows: Prevent overriding the parent project's compiler/linker settings
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
endif()

FetchContent_MakeAvailable(googletest)
include(GoogleTest)

# Add test executables
add_executable(unit_tests
        unit/smoke_test.cpp
        unit/hello_test.cpp
        unit/message_processing_test.cpp
)
target_link_libraries(unit_tests PRIVATE
        IDAWin
        GTest::gtest_main
)
gtest_discover_tests(unit_tests)

target_compile_definitions(unit_tests PRIVATE
        RESOURCE_DIR="${CMAKE_CURRENT_SOURCE_DIR}/unit/resources"
)


#add_executable(integration_tests
#        integration/test_integration.cpp
#)
#target_link_libraries(integration_tests PRIVATE
#        IDAWin
#        GTest::gtest_main
#)
#gtest_discover_tests(integration_tests)


