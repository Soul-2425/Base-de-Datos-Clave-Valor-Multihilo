# Compilador y Banderas
CXX ?= g++
CXXFLAGS ?= -std=c++20 -O3 -Wall -Wextra -Wpedantic -pthread -Iinclude
LDFLAGS ?= -pthread

# Directorios
SRC_DIR = src
INC_DIR = include
TEST_DIR = tests
OBJ_DIR = obj
BIN_DIR = bin

# Archivos Fuente
CORE_SRCS = $(SRC_DIR)/KeyValueStore.cpp \
            $(SRC_DIR)/ThreadPool.cpp \
            $(SRC_DIR)/CommandParser.cpp \
            $(SRC_DIR)/RespParser.cpp \
            $(SRC_DIR)/AofManager.cpp \
            $(SRC_DIR)/MetricsTracker.cpp

SERVER_SRCS = $(SRC_DIR)/main.cpp $(SRC_DIR)/TcpServer.cpp $(CORE_SRCS)
TEST_SRCS = $(TEST_DIR)/test_concurrent.cpp $(CORE_SRCS)

# Archivos Objeto
SERVER_OBJS = $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(SERVER_SRCS))
TEST_OBJS = $(OBJ_DIR)/test_concurrent.o $(patsubst $(SRC_DIR)/%.cpp, $(OBJ_DIR)/%.o, $(CORE_SRCS))

# Binarios
SERVER_BIN = $(BIN_DIR)/kv_server
TEST_BIN = $(BIN_DIR)/kv_tests

.PHONY: all clean test run

all: $(SERVER_BIN) $(TEST_BIN)

# Crear directorios de artefactos
$(OBJ_DIR) $(BIN_DIR):
	mkdir -p $@

# Binario del Servidor
$(SERVER_BIN): $(SERVER_OBJS) | $(BIN_DIR)
	$(CXX) $(SERVER_OBJS) $(LDFLAGS) -o $@
	@echo "-> Servidor generado exitosamente: $@"

# Binario de Pruebas
$(TEST_BIN): $(TEST_OBJS) | $(BIN_DIR)
	$(CXX) $(TEST_OBJS) $(LDFLAGS) -o $@
	@echo "-> Suite de pruebas generada exitosamente: $@"

# Regla de compilación de fuentes de src/
$(OBJ_DIR)/%.o: $(SRC_DIR)/%.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Regla de compilación de tests
$(OBJ_DIR)/test_concurrent.o: $(TEST_DIR)/test_concurrent.cpp | $(OBJ_DIR)
	$(CXX) $(CXXFLAGS) -c $< -o $@

# Ejecutar el servidor con archivo de configuración por defecto
run: $(SERVER_BIN)
	./$(SERVER_BIN) server.conf

# Ejecutar la suite de pruebas
test: $(TEST_BIN)
	./$(TEST_BIN)

# Limpieza
clean:
	rm -rf $(OBJ_DIR) $(BIN_DIR) *.aof
	@echo "-> Artefactos de compilación y persistencia eliminados."
