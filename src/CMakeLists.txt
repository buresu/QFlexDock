add_library(QFlexDock)
add_library(QFlexDock::QFlexDock ALIAS QFlexDock)

target_sources(QFlexDock
    PUBLIC
        FILE_SET HEADERS
        BASE_DIRS ${PROJECT_SOURCE_DIR}/include
        FILES
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/DockManager.h
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/DockPanel.h
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/DockPolicy.h
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/DockTheme.h
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/DockWorkspace.h
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/Global.h
            ${PROJECT_SOURCE_DIR}/include/QFlexDock/NativeWindowAdapter.h
    PRIVATE
        core/DockDragController.cpp
        core/DockDragController.h
        core/DockManager.cpp
        core/DockManager_p.h
        core/DockPanel.cpp
        core/DropZones.cpp
        core/DropZones.h
        core/Global.cpp
        core/LayoutModel.cpp
        core/LayoutModel.h
        core/LayoutSolver.cpp
        core/LayoutSolver.h
        core/LayoutState.cpp
        core/LayoutState.h
        core/SplitterCoordinator.cpp
        core/SplitterCoordinator.h
        gpu/NativeWindowAdapter.cpp
        persistence/LayoutSerializer.cpp
        persistence/LayoutSerializer.h
        widgets/DockAreaWidget.cpp
        widgets/DockAreaWidget.h
        widgets/DockAutoHide.cpp
        widgets/DockAutoHide.h
        widgets/DockColumn.cpp
        widgets/DockColumn.h
        widgets/DockDropOverlay.cpp
        widgets/DockDropOverlay.h
        widgets/DockFloatingWindow.cpp
        widgets/DockFloatingWindow.h
        widgets/DockIcons.cpp
        widgets/DockIcons.h
        widgets/DockSplitHandle.cpp
        widgets/DockSplitHandle.h
        widgets/DockTabBar.cpp
        widgets/DockTabBar.h
        widgets/DockTabGroup.cpp
        widgets/DockTabGroup.h
        widgets/DockTheme.cpp
        widgets/DockWorkspace.cpp
)

target_include_directories(QFlexDock PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})
target_link_libraries(QFlexDock PUBLIC Qt6::Core Qt6::Gui Qt6::Widgets)
if(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
    # The title bar of a window (DockFloatingWindow, Unified frame). Without
    # OBJCXX enabled, CMake compiles the .mm with the C++ compiler, which reads
    # it as Objective-C++ by its extension. Enabling OBJCXX here would not do:
    # in a project that adds this one as a subdirectory, it turns every .mm of
    # the project into OBJCXX, also where no rule to compile it is defined.
    target_sources(QFlexDock PRIVATE
        widgets/DockFloatingWindow_mac.h
        widgets/DockFloatingWindow_mac.mm)
    # The mouse button and the Escape key during a drag (DockDragController),
    # and the title bar.
    target_link_libraries(QFlexDock PRIVATE "-framework CoreGraphics" "-framework AppKit")
endif()
target_compile_definitions(QFlexDock PRIVATE QFLEXDOCK_BUILD)
qflexdock_configure_target(QFlexDock)

get_target_property(_qflexdock_type QFlexDock TYPE)
if(_qflexdock_type STREQUAL "STATIC_LIBRARY")
    target_compile_definitions(QFlexDock PUBLIC QFLEXDOCK_STATIC)
endif()

set_target_properties(QFlexDock PROPERTIES
    VERSION ${PROJECT_VERSION}
    SOVERSION ${PROJECT_VERSION_MAJOR}
    EXPORT_NAME QFlexDock)

if(QFLEXDOCK_INSTALL)
    install(TARGETS QFlexDock
        EXPORT QFlexDockTargets
        ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
        LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
        RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
        FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
endif()

# --- Optional Qt Quick module ------------------------------------------------
# A target of its own so that applications without QML never link Qt Quick.
if(QFLEXDOCK_BUILD_QUICK)
    find_package(Qt6 ${QFLEXDOCK_QT_MIN_VERSION} REQUIRED COMPONENTS Quick QuickWidgets)

    add_library(QFlexDockQuick)
    add_library(QFlexDock::Quick ALIAS QFlexDockQuick)
    target_sources(QFlexDockQuick
        PUBLIC
            FILE_SET HEADERS
            BASE_DIRS ${PROJECT_SOURCE_DIR}/include
            FILES
                ${PROJECT_SOURCE_DIR}/include/QFlexDockQuick/QmlDockController.h
                ${PROJECT_SOURCE_DIR}/include/QFlexDockQuick/QmlPanelAdapter.h
                ${PROJECT_SOURCE_DIR}/include/QFlexDockQuick/QuickGlobal.h
        PRIVATE
            quick/QmlDockController.cpp
            quick/QmlPanelAdapter.cpp
    )
    target_link_libraries(QFlexDockQuick PUBLIC QFlexDock::QFlexDock Qt6::Quick Qt6::QuickWidgets)
    target_compile_definitions(QFlexDockQuick PRIVATE QFLEXDOCKQUICK_BUILD)
    qflexdock_configure_target(QFlexDockQuick)

    get_target_property(_qflexdock_quick_type QFlexDockQuick TYPE)
    if(_qflexdock_quick_type STREQUAL "STATIC_LIBRARY")
        target_compile_definitions(QFlexDockQuick PUBLIC QFLEXDOCKQUICK_STATIC)
    endif()

    set_target_properties(QFlexDockQuick PROPERTIES
        VERSION ${PROJECT_VERSION}
        SOVERSION ${PROJECT_VERSION_MAJOR}
        EXPORT_NAME Quick)

    if(QFLEXDOCK_INSTALL)
        install(TARGETS QFlexDockQuick
            EXPORT QFlexDockQuickTargets
            ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
            LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
            RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
            FILE_SET HEADERS DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})
    endif()
endif()
