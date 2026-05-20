cmake_minimum_required(VERSION 3.22)

project(ubuntu-clip-win
    VERSION 0.5.3
    DESCRIPTION "Windows-style clipboard history popup for Ubuntu"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_AUTOMOC ON)
set(CMAKE_AUTOUIC ON)
set(CMAKE_AUTORCC ON)

include(GNUInstallDirs)
include(CTest)

find_package(Qt6 6.2 REQUIRED COMPONENTS Core Gui Widgets Sql Network)
if(UNIX AND NOT APPLE)
    find_package(X11 REQUIRED)
endif()

add_executable(ubuntu-clip-win
    assets/assets.qrc
    src/AppIntegration.h
    src/AppIntegration.cpp
    src/AppSettings.h
    src/AppSettings.cpp
    src/ClipMime.h
    src/ClipMime.cpp
    src/main.cpp
    src/ClipItem.h
    src/ClipboardStore.h
    src/ClipboardStore.cpp
    src/LinuxHotkeyManager.h
    src/LinuxHotkeyManager.cpp
    src/PasteController.h
    src/PasteController.cpp
    src/PopupWindow.h
    src/PopupWindow.cpp
    src/SettingsDialog.h
    src/SettingsDialog.cpp
    src/SingleInstance.h
    src/SingleInstance.cpp
)

target_link_libraries(ubuntu-clip-win
    PRIVATE
        Qt6::Core
        Qt6::Gui
        Qt6::Widgets
        Qt6::Sql
        Qt6::Network
)
if(UNIX AND NOT APPLE)
    target_link_libraries(ubuntu-clip-win PRIVATE X11::X11)
endif()

target_compile_options(ubuntu-clip-win PRIVATE
    $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
)

install(TARGETS ubuntu-clip-win RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
install(FILES assets/ubuntu-clip-win.svg DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/scalable/apps)
install(FILES assets/ubuntu-clip-win-64.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/64x64/apps RENAME ubuntu-clip-win.png)
install(FILES assets/ubuntu-clip-win-128.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/128x128/apps RENAME ubuntu-clip-win.png)
install(FILES assets/ubuntu-clip-win-256.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/256x256/apps RENAME ubuntu-clip-win.png)
install(FILES assets/ubuntu-clip-win-512.png DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/icons/hicolor/512x512/apps RENAME ubuntu-clip-win.png)
install(FILES packaging/ubuntu-clip-win.desktop DESTINATION ${CMAKE_INSTALL_DATAROOTDIR}/applications)

set(CPACK_PACKAGE_NAME "ubuntu-clip-win")
set(CPACK_PACKAGE_VENDOR "AmirHosein")
set(CPACK_PACKAGE_CONTACT "amirhosein@example.com")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "Windows-style clipboard history popup for Ubuntu")
set(CPACK_GENERATOR "DEB")
set(CPACK_DEBIAN_PACKAGE_SECTION "utils")
set(CPACK_DEBIAN_PACKAGE_PRIORITY "optional")
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_PACKAGE_DEPENDS "libqt6sql6-sqlite, qt6-qpa-plugins, xdotool")
include(CPack)

if(BUILD_TESTING)
    find_package(Qt6 6.2 REQUIRED COMPONENTS Test)

    add_executable(ubuntu-clip-win-test-clipmime
        src/ClipMime.h
        src/ClipMime.cpp
        tests/test_clipmime.cpp
    )

    target_link_libraries(ubuntu-clip-win-test-clipmime
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )

    target_compile_options(ubuntu-clip-win-test-clipmime PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME ubuntu-clip-win-test-clipmime COMMAND ubuntu-clip-win-test-clipmime)

    add_executable(ubuntu-clip-win-test-clipboardstore
        src/AppSettings.h
        src/AppSettings.cpp
        src/ClipMime.h
        src/ClipMime.cpp
        src/ClipItem.h
        src/ClipboardStore.h
        src/ClipboardStore.cpp
        tests/test_clipboardstore.cpp
    )

    target_link_libraries(ubuntu-clip-win-test-clipboardstore
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Sql
            Qt6::Test
            Qt6::Widgets
    )
    target_compile_options(ubuntu-clip-win-test-clipboardstore PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME ubuntu-clip-win-test-clipboardstore COMMAND ubuntu-clip-win-test-clipboardstore)

    add_executable(ubuntu-clip-win-test-appintegration
        src/AppIntegration.h
        src/AppIntegration.cpp
        src/AppSettings.h
        src/AppSettings.cpp
        src/PasteController.h
        src/PasteController.cpp
        tests/test_appintegration.cpp
    )

    target_link_libraries(ubuntu-clip-win-test-appintegration
        PRIVATE
            Qt6::Core
            Qt6::Gui
            Qt6::Test
    )
    target_compile_options(ubuntu-clip-win-test-appintegration PRIVATE
        $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
    )

    add_test(NAME ubuntu-clip-win-test-appintegration COMMAND ubuntu-clip-win-test-appintegration)
endif()
