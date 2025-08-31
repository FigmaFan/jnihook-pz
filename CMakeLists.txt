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
    enable_testing()

    # -------- Java/Dummy-Integrationstests --------
    file(COPY "${PROJECT_SOURCE_DIR}/tests/dummy" DESTINATION .)
    set(DUMMY_DIR "${CMAKE_CURRENT_BINARY_DIR}/dummy")

    # Java-Klassen kompilieren
    file(GLOB TESTS_JAVA_SRC "${DUMMY_DIR}/*.java")
    if(TESTS_JAVA_SRC)
        execute_process(
            COMMAND "${JAVA_HOME}/bin/javac${CMAKE_EXECUTABLE_SUFFIX}" ${TESTS_JAVA_SRC}
            WORKING_DIRECTORY "${DUMMY_DIR}"
        )
    endif()

    # Native Testbibliothek (wird per System.loadLibrary geladen)
    set(TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")
    add_library(hooktest SHARED "${TESTS_DIR}/test.cpp")
    target_link_libraries(hooktest PRIVATE jnihook)
    set_target_properties(hooktest PROPERTIES POSITION_INDEPENDENT_CODE True)

    # Java-basierter Testlauf (separater Testname, kein Konflikt mit GTest)
    add_test(
        NAME jnihook_java_tests
        COMMAND "${JAVA_HOME}/bin/java${CMAKE_EXECUTABLE_SUFFIX}"
                "-Djava.library.path=${CMAKE_BINARY_DIR}"
                "-cp" "${DUMMY_DIR}"
                "dummy.HookTests"
    )

    # -------- GoogleTest-Unit-Tests --------
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
    )
    FetchContent_MakeAvailable(googletest)

    add_executable(jnihook_gtest
        tests/jnihook_init_test.cpp
    )
    target_link_libraries(jnihook_gtest PRIVATE jnihook gtest_main)
    add_test(NAME jnihook_gtest COMMAND jnihook_gtest)
endif()
