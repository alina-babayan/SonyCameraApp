cmake_minimum_required(VERSION 3.16)
project(SonyPhotoEditor LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

find_package(Qt6 REQUIRED COMPONENTS Core Gui Qml Quick QuickControls2)
qt_standard_project_setup(REQUIRES 6.9)

qt_add_executable(SonyPhotoEditor
    main.cpp
    sonycamera.cpp
    liveviewprovider.h
)

qt_add_qml_module(SonyPhotoEditor
    URI SonyPhotoEditor
    VERSION 1.0
    QML_FILES
        Main.qml
    SOURCES

)

set(SONY_SDK_INCLUDE "C:/Users/USER/Desktop/SonySDK/RemoteCli/app/CRSDK")
set(SONY_SDK_LIB     "C:/Users/USER/Desktop/SonySDK/RemoteCli/external/crsdk")

target_include_directories(SonyPhotoEditor PRIVATE
    ${SONY_SDK_INCLUDE}
)

target_link_libraries(SonyPhotoEditor
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Qml
        Qt6::Quick
        Qt6::QuickControls2
        "${SONY_SDK_LIB}/Cr_Core.lib"
)

add_custom_command(TARGET SonyPhotoEditor POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SONY_SDK_LIB}/Cr_Core.dll"
        "${SONY_SDK_LIB}/monitor_protocol.dll"
        "${SONY_SDK_LIB}/monitor_protocol_pf.dll"
        $<TARGET_FILE_DIR:SonyPhotoEditor>

    COMMAND ${CMAKE_COMMAND} -E make_directory
        "$<TARGET_FILE_DIR:SonyPhotoEditor>/CrAdapter"

    COMMAND ${CMAKE_COMMAND} -E copy_if_different
        "${SONY_SDK_LIB}/CrAdapter/Cr_PTP_IP.dll"
        "${SONY_SDK_LIB}/CrAdapter/Cr_PTP_USB.dll"
        "${SONY_SDK_LIB}/CrAdapter/libssh2.dll"
        "${SONY_SDK_LIB}/CrAdapter/libusb-1.0.dll"
        "$<TARGET_FILE_DIR:SonyPhotoEditor>/CrAdapter"
)