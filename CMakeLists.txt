cmake_minimum_required(VERSION 3.21)
project(plumterm LANGUAGES CXX)
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_AUTOMOC ON)
if(MSVC)
  add_compile_options(/utf-8)  # sources hold non-ASCII menu labels
endif()
enable_testing()
find_package(Qt6 REQUIRED COMPONENTS Widgets Concurrent Test Svg)

# Rust core -> static library driven by cargo
set(TP_CORE_DIR ${CMAKE_CURRENT_SOURCE_DIR}/core)
set(TP_CORE_LIB ${CMAKE_CURRENT_BINARY_DIR}/cargo/release/${CMAKE_STATIC_LIBRARY_PREFIX}plumterm_core${CMAKE_STATIC_LIBRARY_SUFFIX})
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
if(WIN32)
  # Windows libraries the Rust standard library and portable-pty call into.
  set_target_properties(plumterm_core PROPERTIES INTERFACE_LINK_LIBRARIES
    "advapi32;bcrypt;dbghelp;kernel32;ntdll;ole32;oleaut32;shell32;user32;userenv;ws2_32")
endif()
add_dependencies(plumterm_core plumterm_core_build)

set(TP_SOURCES
  src/CoreBridge.h
  src/Platform.h
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
)
qt_add_executable(plumterm src/main.cpp ${TP_SOURCES})
target_include_directories(plumterm PRIVATE src)
target_link_libraries(plumterm PRIVATE plumterm_core Qt6::Widgets Qt6::Concurrent Qt6::Svg)
set_target_properties(plumterm PROPERTIES WIN32_EXECUTABLE TRUE)  # no console window behind the app
if(APPLE)
  target_link_libraries(plumterm PRIVATE "-framework CoreFoundation")
endif()

qt_add_executable(corebridge_test tests/corebridge_test.cpp src/CoreBridge.h)
target_include_directories(corebridge_test PRIVATE src)
target_link_libraries(corebridge_test PRIVATE plumterm_core Qt6::Core Qt6::Test)
set_target_properties(corebridge_test PROPERTIES WIN32_EXECUTABLE FALSE)  # keep test output on the console
add_test(NAME corebridge COMMAND corebridge_test)
function(tp_qt_test name)
  qt_add_executable(${name} tests/${name}.cpp src/TerminalScreen.cpp src/TerminalView.cpp src/MenuStyle.cpp)
  target_include_directories(${name} PRIVATE src)
  target_link_libraries(${name} PRIVATE plumterm_core Qt6::Widgets Qt6::Test Qt6::Svg)
  set_target_properties(${name} PROPERTIES WIN32_EXECUTABLE FALSE)
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
