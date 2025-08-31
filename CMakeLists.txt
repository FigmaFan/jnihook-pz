cmake_minimum_required(VERSION 3.21)

project(jnihook LANGUAGES C CXX)

set(CMAKE_CXX_STANDARD 23) # Nur nötig für <jnihook.hpp>. Für <jnihook.h> reicht C++11.
set(JAVA_HOME $ENV{JAVA_HOME} CACHE STRING "Set JAVA_HOME for dependency lookup")
option(JNIHOOK_BUILD_TESTS "Enable building of tests" OFF)

set(JNIHOOK_DIR "${PROJECT_SOURCE_DIR}/src")
set(JNIHOOK_INC "${PROJECT_SOURCE_DIR}/include")
file(GLOB_RECURSE JNIHOOK_SRC "${JNIHOOK_DIR}/*.cpp")

set(JAVA_INCLUDES "${JAVA_HOME}/include")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
    list(APPEND JAVA_INCLUDES "${JAVA_HOME}/include/windows")
else()
    list(APPEND JAVA_INCLUDES "${JAVA_HOME}/include/linux")
endif()

add_library(jnihook STATIC ${JNIHOOK_SRC})
target_include_directories(jnihook PUBLIC ${JNIHOOK_INC} ${JAVA_INCLUDES})
set_target_properties(jnihook PROPERTIES POSITION_INDEPENDENT_CODE True)

# TODO: Add other architectures for OpenJDK8 lookup path
#       since it uses a non-standard path across platforms ($JAVA_HOME/jre/lib/<ARCH>/server)
target_link_directories(jnihook PUBLIC
    "${JAVA_HOME}/lib"
    "${JAVA_HOME}/lib/server"
    "${JAVA_HOME}/jre/lib/amd64/server/"
)
target_link_libraries(jnihook PUBLIC jvm)

if(JNIHOOK_BUILD_TESTS)
    # --- Java/Dummy-Injection-Tests ---
    file(COPY "${PROJECT_SOURCE_DIR}/tests/dummy" DESTINATION .)
    file(GLOB TESTS_JAVA_SRC "${CMAKE_CURRENT_BINARY_DIR}/dummy/*.java")
    # Kompiliere die Dummy-Java-Klassen (configure-time).
    if(TESTS_JAVA_SRC)
        execute_process(
            COMMAND "${JAVA_HOME}/bin/javac${CMAKE_EXECUTABLE_SUFFIX}" ${TESTS_JAVA_SRC}
            WORKING_DIRECTORY "${CMAKE_CURRENT_BINARY_DIR}/dummy"
        )
    endif()

    # Build zu injizierende Shared Library
    set(TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")
    set(TESTS_SRC "${TESTS_DIR}/test.cpp")
    add_library(test SHARED ${TESTS_SRC})
    target_link_libraries(test PRIVATE jnihook)
    set_target_properties(test PROPERTIES POSITION_INDEPENDENT_CODE True)

    # --- GoogleTest-Unit-Tests ---
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    enable_testing()
    add_executable(jnihook_tests
        tests/jnihook_init_test.cpp
    )
    target_link_libraries(jnihook_tests PRIVATE jnihook gtest_main)
    add_test(NAME jnihook_tests COMMAND jnihook_tests)
endif()
