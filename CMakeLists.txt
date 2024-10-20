cmake_minimum_required(VERSION 3.1)
project(easycab C)

add_compile_options(-Wall -g)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(RAYLIB REQUIRED raylib)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(gui src/gui.c)

target_include_directories(gui PRIVATE ${GLIB_INCLUDE_DIRS} ${RAYLIB_INCLUDE_DIRS})

target_link_libraries(gui PRIVATE ${GLIB_LIBRARIES} ${RAYLIB_LIBRARIES} Threads::Threads -pthread ${CMAKE_THREAD_LIBS_INIT})

target_compile_options(gui PRIVATE ${GLIB_CFLAGS_OTHER} ${RAYLIB_CFLAGS_OTHER})