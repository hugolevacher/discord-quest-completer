cmake_minimum_required(VERSION 4.3)
project(Discord_quest C RC)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)

# Third-party JSON parser, built on its own so our warning flags don't apply.
add_library(cjson STATIC third_party/cjson/cJSON.c)
target_include_directories(cjson PUBLIC third_party/cjson)

add_executable(Discord_quest
        src/main.c
        src/msg.c
        src/commands.c
        src/spawn.c
        src/child.c
        src/gui.c
        src/games.c
        src/procs.c
        src/recent.c
        src/update.c
        src/menu.c
        src/http.c
        src/fs.c
        res/app.rc)

target_link_libraries(Discord_quest PRIVATE cjson winhttp user32 gdi32 comctl32 shell32)
target_compile_definitions(Discord_quest PRIVATE WIN32_LEAN_AND_MEAN)

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    target_compile_options(Discord_quest PRIVATE -Wall -Wextra)
elseif(MSVC)
    target_compile_options(Discord_quest PRIVATE /W4)
    target_compile_definitions(Discord_quest PRIVATE _CRT_SECURE_NO_WARNINGS)
endif()

# Link the runtime statically so the exe runs on any Windows machine without
# MinGW DLLs next to it (needed for the release download).
if(MINGW)
    target_link_options(Discord_quest PRIVATE -static)
endif()
