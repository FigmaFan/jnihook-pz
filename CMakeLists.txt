cmake_minimum_required(VERSION 3.21)

project(jnihook LANGUAGES C CXX)

set (CMAKE_CXX_STANDARD 23) # Only necessary for <jnihook.hpp>. If you will use <jnihook.h> only, C++ 11 is fine.
set(JAVA_HOME $ENV{JAVA_HOME} CACHE STRING "Set JAVA_HOME for dependency lookup")
option(JNIHOOK_BUILD_TESTS "Enable building of tests" OFF)

set(JNIHOOK_DIR "${PROJECT_SOURCE_DIR}/src")
set(JNIHOOK_INC "${PROJECT_SOURCE_DIR}/include")
file(GLOB_RECURSE JNIHOOK_SRC "${JNIHOOK_DIR}/*.cpp")

set(JAVA_INCLUDES "${JAVA_HOME}/include")
if(${CMAKE_SYSTEM_NAME} STREQUAL "Windows")
	set(JAVA_INCLUDES ${JAVA_INCLUDES} "${JAVA_HOME}/include/windows")
else()
	set(JAVA_INCLUDES ${JAVA_INCLUDES} "${JAVA_HOME}/include/linux")
endif()

add_library(jnihook STATIC ${JNIHOOK_SRC})
target_include_directories(jnihook PUBLIC ${JNIHOOK_INC} ${JAVA_INCLUDES})
set_target_properties(jnihook PROPERTIES POSITION_INDEPENDENT_CODE True)
# TODO: Add other architectures for OpenJDK8 lookup path
#       since it uses a non-standard path across platforms ($JAVA_HOME/jre/lib/<ARCH>/server)
target_link_directories(jnihook PUBLIC "${JAVA_HOME}/lib" "${JAVA_HOME}/lib/server" "${JAVA_HOME}/jre/lib/amd64/server/")
target_link_libraries(jnihook PUBLIC jvm)

if(JNIHOOK_BUILD_TESTS)
        enable_testing()

        # Build Java classes
        file(COPY "${PROJECT_SOURCE_DIR}/tests/dummy" DESTINATION .)
        execute_process(COMMAND "${JAVA_HOME}/bin/javac${CMAKE_EXECUTABLE_SUFFIX}" "dummy/Dummy.java" "dummy/HookTests.java")

        # Build native test library
        set(TESTS_DIR "${PROJECT_SOURCE_DIR}/tests")
        set(TESTS_SRC "${TESTS_DIR}/test.cpp")
        add_library(hooktest SHARED ${TESTS_SRC})
        target_link_libraries(hooktest PRIVATE jnihook)
        set_target_properties(hooktest PROPERTIES POSITION_INDEPENDENT_CODE True)

        add_test(NAME jnihook_tests COMMAND "${JAVA_HOME}/bin/java${CMAKE_EXECUTABLE_SUFFIX}" "-Djava.library.path=${CMAKE_BINARY_DIR}" "-cp" "${CMAKE_BINARY_DIR}" "dummy.HookTests")
endif()
