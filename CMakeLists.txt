cmake_minimum_required(VERSION 3.14)
project(RedisStyleKVStore VERSION 2.0.0 LANGUAGES CXX)

# Estándar C++20 moderno
set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Opciones de compilación avanzadas
if(MSVC)
    add_compile_options(/W4 /EHsc /O2 /utf-8)
else()
    add_compile_options(-Wall -Wextra -Wpedantic -Wconversion -O3)
endif()

# Localizar biblioteca de hilos
find_package(Threads REQUIRED)

# Directorio de cabeceras
include_directories(include)

# Fuentes comunes del núcleo del motor
set(CORE_SOURCES
    src/KeyValueStore.cpp
    src/ThreadPool.cpp
    src/CommandParser.cpp
    src/RespParser.cpp
    src/AofManager.cpp
    src/MetricsTracker.cpp
)

# 1. Servidor Principal
add_executable(kv_server
    src/main.cpp
    src/TcpServer.cpp
    ${CORE_SOURCES}
)

target_link_libraries(kv_server PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(kv_server PRIVATE ws2_32)
endif()

# 2. Suite de Pruebas Integrales y de Estrés
add_executable(kv_tests
    tests/test_concurrent.cpp
    ${CORE_SOURCES}
)

target_link_libraries(kv_tests PRIVATE Threads::Threads)
if(WIN32)
    target_link_libraries(kv_tests PRIVATE ws2_32)
endif()

# Soporte para ejecución con ctest
enable_testing()
add_test(NAME EngineTestSuite COMMAND kv_tests)
