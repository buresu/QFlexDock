cmake_minimum_required(VERSION 3.23)

project(QFlexDock
    VERSION 0.1.0
    DESCRIPTION "Flexible docking library for Qt 6 Widgets"
    LANGUAGES CXX)

set(QFLEXDOCK_QT_MIN_VERSION 6.8)

find_package(Qt6 ${QFLEXDOCK_QT_MIN_VERSION} REQUIRED COMPONENTS Core Gui Widgets)
find_package(Qt6 ${QFLEXDOCK_QT_MIN_VERSION} QUIET COMPONENTS Quick QuickWidgets)

if(Qt6Quick_FOUND AND Qt6QuickWidgets_FOUND)
    set(_qflexdock_quick_default ON)
else()
    set(_qflexdock_quick_default OFF)
endif()

option(QFLEXDOCK_BUILD_QUICK "Build the optional QFlexDock::Quick module (needs Qt Quick)" ${_qflexdock_quick_default})
option(QFLEXDOCK_BUILD_EXAMPLES "Build the examples" ${PROJECT_IS_TOP_LEVEL})
option(QFLEXDOCK_BUILD_TESTS "Build the tests (also needs BUILD_TESTING)" ${PROJECT_IS_TOP_LEVEL})
option(QFLEXDOCK_INSTALL "Generate install rules" ${PROJECT_IS_TOP_LEVEL})
option(QFLEXDOCK_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
set(QFLEXDOCK_SANITIZERS "" CACHE STRING "Sanitizers to enable, e.g. \"address,undefined\" (GCC/Clang)")

set(CMAKE_AUTOMOC ON)
set(CMAKE_CXX_VISIBILITY_PRESET hidden)
set(CMAKE_VISIBILITY_INLINES_HIDDEN ON)

include(GNUInstallDirs)

# Shared compile settings for every QFlexDock target (library, tests, examples).
function(qflexdock_configure_target target)
    target_compile_features(${target} PUBLIC cxx_std_20)
    set_target_properties(${target} PROPERTIES CXX_EXTENSIONS OFF)
    target_compile_definitions(${target} PRIVATE
        QT_NO_CAST_FROM_ASCII
        QT_NO_CAST_TO_ASCII
        QT_NO_KEYWORDS
        QT_DISABLE_DEPRECATED_UP_TO=0x060800)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(QFLEXDOCK_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic -Wshadow -Wconversion)
        if(QFLEXDOCK_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
        if(QFLEXDOCK_SANITIZERS)
            target_compile_options(${target} PRIVATE -fsanitize=${QFLEXDOCK_SANITIZERS} -fno-omit-frame-pointer)
            target_link_options(${target} PUBLIC -fsanitize=${QFLEXDOCK_SANITIZERS})
        endif()
    endif()
endfunction()

add_subdirectory(src)

if(QFLEXDOCK_BUILD_EXAMPLES)
    add_subdirectory(examples)
endif()

if(QFLEXDOCK_BUILD_TESTS)
    include(CTest)
    if(BUILD_TESTING)
        add_subdirectory(tests)
    endif()
endif()

if(QFLEXDOCK_INSTALL)
    include(CMakePackageConfigHelpers)
    set(_qflexdock_cmake_dir ${CMAKE_INSTALL_LIBDIR}/cmake/QFlexDock)

    install(EXPORT QFlexDockTargets
        NAMESPACE QFlexDock::
        DESTINATION ${_qflexdock_cmake_dir})
    if(TARGET QFlexDockQuick)
        install(EXPORT QFlexDockQuickTargets
            NAMESPACE QFlexDock::
            DESTINATION ${_qflexdock_cmake_dir})
    endif()

    configure_package_config_file(
        cmake/QFlexDockConfig.cmake.in
        ${CMAKE_CURRENT_BINARY_DIR}/QFlexDockConfig.cmake
        INSTALL_DESTINATION ${_qflexdock_cmake_dir})
    write_basic_package_version_file(
        ${CMAKE_CURRENT_BINARY_DIR}/QFlexDockConfigVersion.cmake
        VERSION ${PROJECT_VERSION}
        COMPATIBILITY SameMinorVersion)
    install(FILES
        ${CMAKE_CURRENT_BINARY_DIR}/QFlexDockConfig.cmake
        ${CMAKE_CURRENT_BINARY_DIR}/QFlexDockConfigVersion.cmake
        DESTINATION ${_qflexdock_cmake_dir})
    install(FILES LICENSE DESTINATION ${CMAKE_INSTALL_DATADIR}/licenses/QFlexDock)
endif()
