cmake_minimum_required(VERSION 3.21)
project(plumterm LANGUAGES CXX)
if(APPLE)
  enable_language(OBJCXX)
endif()
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Test Svg Network)

# Rust core -> static library driven by cargo
set(TP_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core)
if(MSVC)
  set(TP_CORE_LIB ${CMAKE_CURRENT_BINARY_DIR}/cargo/release/plumterm_core.lib)
else()
  set(TP_CORE_LIB ${CMAKE_CURRENT_BINARY_DIR}/cargo/release/libplumterm_core.a)
endif()
file(GLOB TP_CORE_RS ${TP_CORE_DIR}/src/*.rs)
add_custom_command(
  OUTPUT ${TP_CORE_LIB}
  COMMAND cargo build --release --manifest-path ${TP_CORE_DIR}/Cargo.toml --target-dir ${CMAKE_CURRENT_BINARY_DIR}/cargo
  DEPENDS ${TP_CORE_RS} ${TP_CORE_DIR}/Cargo.toml
  COMMENT "Building Rust core")
add_custom_target(plumterm_core_build DEPENDS ${TP_CORE_LIB})
add_library(plumterm_core STATIC IMPORTED GLOBAL)
set_target_properties(plumterm_core PROPERTIES IMPORTED_LOCATION ${TP_CORE_LIB}
  INTERFACE_INCLUDE_DIRECTORIES ${TP_CORE_DIR})
add_dependencies(plumterm_core plumterm_core_build)

set(TP_SOURCES
  src/CoreBridge.h
  src/MenuStyle.cpp src/MenuStyle.h
  src/TerminalScreen.cpp src/TerminalScreen.h
  src/TerminalView.cpp src/TerminalView.h
  src/PaneHeader.cpp src/PaneHeader.h
  src/TerminalPane.cpp src/TerminalPane.h
  src/TabBar.cpp src/TabBar.h
  src/SearchBar.cpp src/SearchBar.h
  src/AskAiBar.cpp src/AskAiBar.h
  src/TaskBar.cpp src/TaskBar.h
  src/TaskOutputDialog.cpp src/TaskOutputDialog.h
  src/DiffViewerDialog.cpp src/DiffViewerDialog.h
  src/CommitDialog.cpp src/CommitDialog.h
  src/SettingsDialog.cpp src/SettingsDialog.h
  src/MainWindow.cpp src/MainWindow.h
  src/GlobalHotkey.cpp src/GlobalHotkey.h
)

# Global show/hide hotkey backend.
if(APPLE)
  list(APPEND TP_SOURCES src/GlobalHotkey_mac.mm)
  set(TP_HOTKEY_LIBS "-framework Carbon" "-framework AppKit")
elseif(UNIX)
  find_package(X11)
  if(X11_FOUND AND X11_Xkb_FOUND)
    list(APPEND TP_SOURCES src/GlobalHotkey_x11.cpp)
    set(TP_HOTKEY_LIBS X11::X11)
  else()
    message(WARNING "libX11 not found: global hotkey disabled (plumterm --toggle still works)")
    list(APPEND TP_SOURCES src/GlobalHotkey_none.cpp)
  endif()
else()
  list(APPEND TP_SOURCES src/GlobalHotkey_none.cpp)
endif()
qt_add_executable(plumterm src/main.cpp ${TP_SOURCES})
target_include_directories(plumterm PRIVATE src)
target_link_libraries(plumterm PRIVATE plumterm_core Qt6::Widgets Qt6::Concurrent Qt6::Svg Qt6::Network ${TP_HOTKEY_LIBS})
if(APPLE)
  target_link_libraries(plumterm PRIVATE "-framework CoreFoundation")
endif()

qt_add_executable(corebridge_test tests/corebridge_test.cpp src/CoreBridge.h)
target_include_directories(corebridge_test PRIVATE src)
target_link_libraries(corebridge_test PRIVATE plumterm_core Qt6::Core Qt6::Test)
add_test(NAME corebridge COMMAND corebridge_test)
function(tp_qt_test name)
  qt_add_executable(${name} tests/${name}.cpp src/TerminalScreen.cpp src/TerminalView.cpp src/MenuStyle.cpp)
  target_include_directories(${name} PRIVATE src)
  target_link_libraries(${name} PRIVATE plumterm_core Qt6::Widgets Qt6::Test Qt6::Svg)
  if(APPLE)
    target_link_libraries(${name} PRIVATE "-framework CoreFoundation")
  endif()
  add_test(NAME ${name} COMMAND ${name})
  set_tests_properties(${name} PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
endfunction()
tp_qt_test(terminal_screen_test)
tp_qt_test(terminalview_test)

add_test(NAME core_rust COMMAND cargo test --manifest-path ${TP_CORE_DIR}/Cargo.toml)
add_test(NAME smoke COMMAND plumterm)
set_tests_properties(smoke PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen;TP_HEADLESS_EXIT=1")

# Install + Debian package (cpack -G DEB)
include(GNUInstallDirs)
install(TARGETS plumterm RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})
if(UNIX AND NOT APPLE)
  install(FILES packaging/plumterm.desktop DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
  foreach(sz 48 64 128 256 512)
    install(FILES packaging/plumterm-${sz}.png
            DESTINATION ${CMAKE_INSTALL_DATADIR}/icons/hicolor/${sz}x${sz}/apps RENAME plumterm.png)
  endforeach()
  install(FILES packaging/io.github.pluminate.plumterm.metainfo.xml
          DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
endif()
set(CPACK_PACKAGE_NAME plumterm)
set(CPACK_PACKAGE_VERSION "$ENV{PLUMTERM_VERSION}")
if(NOT CPACK_PACKAGE_VERSION)
  set(CPACK_PACKAGE_VERSION "1.1.0")
endif()
set(CPACK_PACKAGE_CONTACT "Pluminate")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/Pluminate/plumterm")
set(CPACK_DEBIAN_PACKAGE_HOMEPAGE "https://github.com/Pluminate/plumterm")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "PlumTerm terminal emulator")
set(CPACK_GENERATOR DEB)
set(CPACK_DEBIAN_PACKAGE_SECTION utils)
set(CPACK_DEBIAN_PACKAGE_SHLIBDEPS ON)
set(CPACK_DEBIAN_FILE_NAME DEB-DEFAULT)
include(CPack)
