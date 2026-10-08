# Not part of the test suite: a stand-alone project that uses an *installed*
# QFlexDock through find_package(), to check the install rules and the package
# config. Configure it with -DCMAKE_PREFIX_PATH=<install prefix>.
cmake_minimum_required(VERSION 3.23)
project(QFlexDockConsumer LANGUAGES CXX)

find_package(QFlexDock 0.1 REQUIRED)

add_executable(consumer main.cpp)
target_link_libraries(consumer PRIVATE QFlexDock::QFlexDock)

# The Quick component is optional; when the installation has it, use it.
find_package(QFlexDock 0.1 QUIET COMPONENTS Quick)
if(QFlexDock_Quick_FOUND)
    add_executable(consumer_quick main_quick.cpp)
    target_link_libraries(consumer_quick PRIVATE QFlexDock::Quick)
endif()
