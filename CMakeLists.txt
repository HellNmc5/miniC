cmake_minimum_required(VERSION 3.20)
project(miniC LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

if(MSVC)
    add_compile_options(/utf-8 /W3)   # исходники в UTF-8 — как флаг в свойствах проекта
endif()

set(SRC ${CMAKE_SOURCE_DIR}/miniC)
set(TP  ${SRC}/third_party)

# Ядро компилятора — общее для обеих программ
add_library(minic_core STATIC
    ${SRC}/Lexer.cpp
    ${SRC}/IdTable.cpp
    ${SRC}/Ast.cpp
    ${SRC}/Parser.cpp
)
target_include_directories(minic_core PUBLIC ${SRC})

# Консольная версия — для проверок
add_executable(minic_cli ${SRC}/main.cpp)
target_link_libraries(minic_cli PRIVATE minic_core)

# Интерфейс; WIN32 — то же, что подсистема Windows
add_executable(minic_gui WIN32
    ${SRC}/gui.cpp
    ${SRC}/gui_platform.cpp
    ${TP}/imgui/imgui.cpp
    ${TP}/imgui/imgui_draw.cpp
    ${TP}/imgui/imgui_tables.cpp
    ${TP}/imgui/imgui_widgets.cpp
    ${TP}/imgui/backends/imgui_impl_win32.cpp
    ${TP}/imgui/backends/imgui_impl_dx11.cpp
    ${TP}/ImGuiColorTextEdit/TextEditor.cpp
)
target_include_directories(minic_gui PRIVATE
    ${TP}/imgui ${TP}/imgui/backends ${TP}/ImGuiColorTextEdit)
target_link_libraries(minic_gui PRIVATE minic_core
    d3d11 d3dcompiler dwmapi comdlg32 shell32 imm32)