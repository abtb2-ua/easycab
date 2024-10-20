cmake_minimum_required(VERSION 3.1)
project(easycab C)

add_compile_options(-Wall -g)

find_package(PkgConfig REQUIRED)
pkg_check_modules(GLIB REQUIRED glib-2.0)
pkg_check_modules(RAYLIB REQUIRED raylib)
pkg_check_modules(KAFKA REQUIRED rdkafka)
pkg_check_modules(UUID REQUIRED uuid)
pkg_check_modules(NCURSES REQUIRED ncurses)

# Busca las banderas de compilación y vinculación usando mysql_config
execute_process(
    COMMAND mysql_config --cflags
    OUTPUT_VARIABLE MYSQL_CFLAGS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

execute_process(
    COMMAND mysql_config --libs
    OUTPUT_VARIABLE MYSQL_LIBS
    OUTPUT_STRIP_TRAILING_WHITESPACE
)

set(THREADS_PREFER_PTHREAD_FLAG ON)
find_package(Threads REQUIRED)

add_executable(gui src/gui.c)
add_executable(EC_Central src/EC_Central.c src/ncurses_gui.c src/common.c src/kafka_module.c src/socket_module.c)  
add_executable(EC_DE src/EC_DE.c src/common.c)

target_include_directories(gui PRIVATE ${GLIB_INCLUDE_DIRS} ${RAYLIB_INCLUDE_DIRS})
target_include_directories(EC_Central PRIVATE ${GLIB_INCLUDE_DIRS} ${MYSQL_INCLUDE_DIRS} 
                            ${KAFKA_INCLUDE_DIRS} ${NCURSES_INCLUDE_DIRS} ${UUID_INCLUDE_DIRS})
# target_include_directories(EC_DE PRIVATE ${GLIB_INCLUDE_DIRS} ${KAFKA_INCLUDE_DIRS} ${UUID_INCLUDE_DIRS})
target_include_directories(EC_DE PRIVATE ${GLIB_INCLUDE_DIRS} ${KAFKA_INCLUDE_DIRS} ${UUID_INCLUDE_DIRS})

target_link_libraries(gui PRIVATE ${GLIB_LIBRARIES} ${RAYLIB_LIBRARIES} Threads::Threads)
target_link_libraries(EC_Central PRIVATE ${GLIB_LIBRARIES} Threads::Threads ${MYSQL_LIBS} 
                        ${KAFKA_LIBRARIES} ${NCURSES_LIBRARIES} ${UUID_LIBRARIES})
target_link_libraries(EC_DE PRIVATE ${GLIB_LIBRARIES} Threads::Threads ${KAFKA_LIBRARIES} ${UUID_LIBRARIES})
# target_include_directories(EC_DE PRIVATE ${GLIB_LIBRARIES} Threads::Threads ${KAFKA_LIBRARIES} ${UUID_LIBRARIES})

target_compile_options(gui PRIVATE ${GLIB_CFLAGS_OTHER} ${RAYLIB_CFLAGS_OTHER})
target_compile_options(EC_Central PRIVATE ${GLIB_CFLAGS_OTHER} ${MYSQL_CFLAGS} 
                        ${KAFKA_CFLAGS_OTHER} ${NCURSES_CFLAGS_OTHER} ${UUID_CFLAGS_OTHER})
target_compile_options(EC_DE PRIVATE ${GLIB_CFLAGS_OTHER} ${KAFKA_CFLAGS_OTHER} ${UUID_CFLAGS_OTHER}) 
# target_compile_options(EC_DE PRIVATE ${GLIB_CFLAGS_OTHER} ${KAFKA_CFLAGS_OTHER} ${UUID_CFLAGS_OTHER})
