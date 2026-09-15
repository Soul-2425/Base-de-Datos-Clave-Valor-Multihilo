# ==============================================================================
# Multi-stage Dockerfile para Redis-Style C++20 Database Engine
# ==============================================================================

# ETAPA 1: Compilación y Enlace
FROM gcc:13-bookworm AS builder

WORKDIR /usr/src/app

# Instalar CMake
RUN apt-get update && apt-get install -y cmake && rm -rf /var/lib/apt/lists/*

# Copiar código fuente
COPY CMakeLists.txt server.conf ./
COPY include/ ./include/
COPY src/ ./src/
COPY tests/ ./tests/

# Compilar en modo Release con optimizaciones O3
RUN mkdir build && cd build && \
    cmake .. -DCMAKE_BUILD_TYPE=Release && \
    cmake --build . -j$(nproc)

# Ejecutar suite de pruebas dentro del contenedor de build para asegurar integridad
RUN ./build/kv_tests

# ==============================================================================
# ETAPA 2: Imagen Final Minimalista de Producción
FROM debian:bookworm-slim AS runtime

WORKDIR /app

# Instalar bibliotecas mínimas necesarias
RUN apt-get update && apt-get install -y libstdc++6 && rm -rf /var/lib/apt/lists/*

# Copiar binarios y configuración desde el builder
COPY --from=builder /usr/src/app/build/kv_server /app/kv_server
COPY --from=builder /usr/src/app/server.conf /app/server.conf

# Crear usuario sin privilegios para ejecución segura
RUN useradd -u 1001 appuser && \
    mkdir -p /app/data && \
    chown -R appuser:appuser /app

USER appuser

EXPOSE 8080

ENTRYPOINT ["/app/kv_server", "/app/server.conf"]
