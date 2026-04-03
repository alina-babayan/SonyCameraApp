cmake_minimum_required(VERSION 3.16)
project(SonyCameraApp LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.9)

qt_add_executable(SonyCameraApp
    main.cpp
    sonycamera.cpp
    liveviewprovider.h
    sonycamera.h)

qt_add_qml_module(SonyCameraApp
    URI SonyPhotoEditor
    VERSION 1.0
    QML_FILES
        Main.qml
    SOURCES
)
set(SONY_SDK_INCLUDE "/home/tigran/Downloads/project/redist/scrsdk/include/SCRSDK")
set(SONY_SDK_LIB     "/home/tigran/Downloads/project/redist/scrsdk/lib_amd64")

target_include_directories(SonyCameraApp PRIVATE
    ${SONY_SDK_INCLUDE}
)

target_link_libraries(SonyCameraApp
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        "${SONY_SDK_LIB}/libCr_Core.so"
)
add_custom_command(TARGET SonyCameraApp POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SONY_SDK_LIB}/libCr_Core.so"
        $<TARGET_FILE_DIR:SonyCameraApp>
    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:SonyCameraApp>/CrAdapter"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SONY_SDK_LIB}/CrAdapter/libCr_PTP_USB.so"
        "$<TARGET_FILE_DIR:SonyCameraApp>/CrAdapter"
)
