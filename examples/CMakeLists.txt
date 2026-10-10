function(qflexdock_add_example name)
    add_executable(${name} ${ARGN})
    target_link_libraries(${name} PRIVATE QFlexDock::QFlexDock)
    target_compile_features(${name} PRIVATE cxx_std_20)
endfunction()

qflexdock_add_example(qflexdock-basic basic/main.cpp)
qflexdock_add_example(qflexdock-multi-window multi-window/main.cpp)
qflexdock_add_example(qflexdock-obs-style obs-style/main.cpp)
qflexdock_add_example(qflexdock-vscode-style vscode-style/main.cpp)
qflexdock_add_example(qflexdock-chrome-style chrome-style/main.cpp)
qflexdock_add_example(qflexdock-vs-style vs-style/main.cpp)
qflexdock_add_example(qflexdock-photoshop-style photoshop-style/main.cpp)

if(TARGET QFlexDockQuick)
    qflexdock_add_example(qflexdock-quick-panel quick-panel/main.cpp)
    target_link_libraries(qflexdock-quick-panel PRIVATE QFlexDock::Quick)
    qt_add_resources(qflexdock-quick-panel "quick_panel_qml"
        PREFIX "/"
        BASE quick-panel
        FILES quick-panel/Panel.qml)
endif()

find_package(Qt6 ${QFLEXDOCK_QT_MIN_VERSION} QUIET COMPONENTS OpenGLWidgets)
if(Qt6OpenGLWidgets_FOUND)
    qflexdock_add_example(qflexdock-gpu-panel gpu-panel/main.cpp)
    target_link_libraries(qflexdock-gpu-panel PRIVATE Qt6::OpenGLWidgets)
endif()
