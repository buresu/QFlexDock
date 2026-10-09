find_package(Qt6 ${QFLEXDOCK_QT_MIN_VERSION} REQUIRED COMPONENTS Test)

# qflexdock_add_test(<name> [GUI] [LIBS ...])
# GUI tests run on the offscreen platform unless QFLEXDOCK_TEST_PLATFORM says
# otherwise (e.g. "xcb" under xvfb-run, or "wayland").
set(QFLEXDOCK_TEST_PLATFORM "offscreen" CACHE STRING "QT_QPA_PLATFORM used by GUI tests")

function(qflexdock_add_test name)
    cmake_parse_arguments(ARG "GUI" "" "LIBS" ${ARGN})
    add_executable(${name} ${name}.cpp)
    target_link_libraries(${name} PRIVATE QFlexDock::QFlexDock Qt6::Test ${ARG_LIBS})
    # Tests may reach into non-installed headers to check internals directly.
    target_include_directories(${name} PRIVATE ${PROJECT_SOURCE_DIR}/src)
    qflexdock_configure_target(${name})
    # QTRY_* in Qt 6.8 and 6.10 narrow a chrono count to int inside the macro,
    # which GCC reports at the place of use even for a system header.
    if(NOT MSVC)
        target_compile_options(${name} PRIVATE -Wno-conversion)
    endif()
    add_test(NAME ${name} COMMAND ${name})
    # An empty QFLEXDOCK_TEST_PLATFORM leaves the choice to the environment.
    if(ARG_GUI AND QFLEXDOCK_TEST_PLATFORM)
        set_tests_properties(${name} PROPERTIES
            ENVIRONMENT "QT_QPA_PLATFORM=${QFLEXDOCK_TEST_PLATFORM}")
    endif()
endfunction()

qflexdock_add_test(tst_smoke)
qflexdock_add_test(tst_layoutmodel)
qflexdock_add_test(tst_layoutsolver)
qflexdock_add_test(tst_manager GUI LIBS Qt6::Widgets)
qflexdock_add_test(tst_dragdrop GUI LIBS Qt6::Widgets)
qflexdock_add_test(tst_splitter GUI LIBS Qt6::Widgets)
qflexdock_add_test(tst_persistence GUI LIBS Qt6::Widgets)
qflexdock_add_test(tst_style GUI LIBS Qt6::Widgets)

# Native windows and GPU widgets. QOpenGLWidget is covered when Qt has it; the
# test itself skips where no OpenGL context can be created (e.g. "offscreen").
find_package(Qt6 ${QFLEXDOCK_QT_MIN_VERSION} QUIET COMPONENTS OpenGLWidgets)
if(Qt6OpenGLWidgets_FOUND)
    qflexdock_add_test(tst_gpu GUI LIBS Qt6::Widgets Qt6::OpenGLWidgets)
    target_compile_definitions(tst_gpu PRIVATE QFLEXDOCK_TEST_HAS_OPENGLWIDGETS)
else()
    qflexdock_add_test(tst_gpu GUI LIBS Qt6::Widgets)
endif()

if(TARGET QFlexDockQuick)
    qflexdock_add_test(tst_quick GUI LIBS QFlexDock::Quick)
endif()
qflexdock_add_test(tst_realdrag GUI LIBS Qt6::Widgets)
qflexdock_add_test(tst_scale GUI LIBS Qt6::Widgets)
qflexdock_add_test(tst_floating GUI LIBS Qt6::Widgets)
