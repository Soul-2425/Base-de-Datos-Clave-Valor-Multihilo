# 🚀 High-Performance In-Memory Key-Value Database (C++20)

[![CI Pipeline](https://github.com/Soul-2425/Base-de-Datos-Clave-Valor-Multihilo/actions/workflows/ci.yml/badge.svg)](https://github.com/Soul-2425/Base-de-Datos-Clave-Valor-Multihilo/actions)
[![C++20](https://img.shields.io/badge/C%2B%2B-20-blue.svg?style=flat&logo=c%2B%2B)](https://en.cppreference.com/w/cpp/20)
[![CMake](https://img.shields.io/badge/Build-CMake%203.14%2B-green.svg?logo=cmake)](https://cmake.org/)
[![Docker](https://img.shields.io/badge/Docker-Ready-2496ED.svg?logo=docker&logoColor=white)](Dockerfile)
[![Protocol](https://img.shields.io/badge/Protocol-RESP2%20Compatible-red.svg)](https://redis.io/docs/reference/protocol-spec/)
[![License](https://img.shields.io/badge/License-MIT-yellow.svg)](LICENSE)
[![Platform](https://img.shields.io/badge/Platform-Linux%20%7C%20WSL%20%7C%20Windows%20%7C%20macOS-lightgrey.svg)]()

Motor de almacenamiento clave-valor en memoria de nivel comercial y grado de producción, construido desde cero en **C++ moderno (C++20)**.

Diseñado con una arquitectura de red reactiva no bloqueante (**Reactor Pattern**), almacenamiento particionado (**64x Sharded Striped Locking** con `alignas(64)` para eliminar la contención global y el *False Sharing*), estructuras de datos avanzadas (**Strings, Hashes, Listas y Contadores Atómicos**), persistencia en disco con compactación en caliente (**Append-Only File - AOF & BGREWRITEAOF**), ciclo de vida de claves (**Dual TTL + LRU Eviction**), compatibilidad con el protocolo oficial **RESP2** y contenedorización lista para despliegue con **Docker**.

---

## 🏛️ Arquitectura del Sistema

```text
               +-------------------------------------------------------------+
               |     Clientes TCP (redis-cli, Python, Node.js, Go, Netcat)   |
               +-------------------------------------------------------------+
                                              |
                                              v
               +-------------------------------------------------------------+
               |       Reactor Event Loop (poll / epoll no bloqueante)       |
               |       -> Resuelve C10K: Sockets idle no atan ningún hilo    |
               +-------------------------------------------------------------+
                                              | (Eventos de lectura)
                                              v
               +-------------------------------------------------------------+
               |        ThreadPool Worker Pool (Hardware Concurrency)        |
               +-------------------------------------------------------------+
                                              |
                                              v
               +-------------------------------------------------------------+
               |   RespParser: Decodificación RESP2 + Fallback Texto Plano   |
               +-------------------------------------------------------------+
                         |                    |                     |
                         v                    v                     v
              +--------------------+ +------------------+ +------------------+
              |   Comandos Auth/   | |  Hashes, Listas, | | Persistencia AOF |
              |   Info / Metrics   | |  Strings, INCR   | |  & BGREWRITEAOF  |
              +--------------------+ +------------------+ +------------------+
                                              |
                                              v
              +---------------------------------------------------------------+
              |            Sharded KeyValueStore (64 Particiones)             |
              |       (Hash(key) % 64 con alineación de caché alignas(64))    |
              |   Shard 0      Shard 1      Shard 2     ...     Shard 63      |
              |  [RW-Lock]    [RW-Lock]    [RW-Lock]           [RW-Lock]      |
              |  [Strings]    [Strings]    [Strings]           [Strings]      |
              |  [Hashes ]    [Hashes ]    [Hashes ]           [Hashes ]      |
              |  [Listas ]    [Listas ]    [Listas ]           [Listas ]      |
              +---------------------------------------------------------------+
                                              ^
                                              |
              +---------------------------------------------------------------+
              |                  Background Janitor Thread                    |
              |   - Expiración activa de claves con TTL caducado (100 ms)     |
              |   - Desalojo LRU si se supera el umbral de max_keys           |
              +---------------------------------------------------------------+
```

---

## ⚡ Características Principales (100% Producción)

### 1. Concurrencia y Sincronización de Élite
* **64x Sharded Striped Locking**: El espacio de claves se particiona en 64 shards independientes (`std::shared_mutex`).
* **Prevención de False Sharing (`alignas(64)`)**: Cada estructura de shard está alineada al tamaño de la línea de caché de la CPU para evitar invalidación cruzada entre núcleos L1/L2.
* **Patrón Reader-Writer Lock**: Concurrencia masiva en lecturas simultáneas (`std::shared_lock`) sin contención ni serialización.

### 2. Reactor Pattern (Resolución del problema C10K)
* Arquitectura basada en eventos con sockets no bloqueantes (`O_NONBLOCK`).
* Las conexiones inactivas (*idle*) no consumen hilos del `ThreadPool`. Los hilos trabajadores solo se activan ante eventos reales de I/O.

### 3. Estructuras de Datos Ricas
* **Strings & Contadores**: `SET`, `GET`, `MSET`, `MGET`, `INCR`, `DECR`, `INCRBY`.
* **Hashes (Diccionarios Anidados)**: `HSET`, `HGET`, `HDEL`, `HEXISTS`, `HLEN`, `HGETALL`.
* **Listas (Deques)**: `LPUSH`, `RPUSH`, `LPOP`, `RPOP`, `LLEN`, `LRANGE`.

### 4. Persistencia en Disco y Compactación en Caliente
* **Append-Only File (AOF)**: Registro atómico secuencial de operaciones mutadoras.
* **Reconstrucción Automática**: Replay instantáneo al reiniciar el servidor.
* **AOF Rewrite (`BGREWRITEAOF`)**: Compactación atómica del historial generando una instantánea mínima en disco para evitar que el log crezca indefinidamente.

### 5. Ciclo de Vida de Claves y Memoria (TTL + LRU)
* **Dual TTL**: Expiración pasiva en consultas (`GET`, `EXISTS`) y purga activa mediante el hilo *Janitor* en background.
* **Política de Desalojo LRU**: Purga automática de las claves menos recientemente usadas cuando se supera el límite `max_keys`.

### 6. DevOps, Docker y CI/CD
* **Docker Multi-Stage**: Imagen de producción minimalista (< 25 MB) corriendo bajo usuario sin privilegios.
* **Docker Compose**: Despliegue con un solo comando con persistencia de volúmenes.
* **GitHub Actions CI**: Pipeline automatizado que compila y ejecuta las 9 suites de pruebas en Ubuntu, Windows y macOS.

---

## 🛠️ Compilación y Despliegue

### Opción 1: Con Docker (Recomendado)

```bash
# Construir y levantar con docker-compose
docker-compose up -d

# Ver logs en tiempo real
docker-compose logs -f
```

O construyendo la imagen manualmente:
```bash
docker build -t redis-cpp:latest .
docker run -d -p 8080:8080 -v $(pwd)/data:/app/data redis-cpp:latest
```

### Opción 2: Compilación Nativa con CMake

```bash
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Ejecutar el servidor
./kv_server ../server.conf

# Ejecutar la suite integral de pruebas (9/9 pruebas)
./kv_tests
```

### Opción 3: Compilación con Makefile (Linux / WSL)

```bash
make all        # Compila servidor y suite de pruebas
make test       # Ejecuta los tests automatizados
make run        # Inicia el servidor con server.conf
```

---

## 💬 Matriz de Comandos Soportados

| Categoría | Comando | Sintaxis | Descripción |
| :--- | :--- | :--- | :--- |
| **Strings** | `SET` | `SET <key> <val> [EX sec]` | Inserta/actualiza con TTL opcional. |
| | `GET` | `GET <key>` | Obtiene valor o `(nil)` si no existe/expiró. |
| | `MSET` | `MSET <k1> <v1> <k2> <v2>...` | Inserción atómica multi-clave. |
| | `MGET` | `MGET <k1> <k2>...` | Lectura multi-clave en array RESP. |
| **Contadores**| `INCR` | `INCR <key>` | Incrementa en 1 de forma atómica. |
| | `DECR` | `DECR <key>` | Decrementa en 1 de forma atómica. |
| | `INCRBY` | `INCRBY <key> <delta>` | Suma un valor entero arbitrario. |
| **Hashes** | `HSET` | `HSET <key> <campo> <val>` | Asigna un campo en el hash. |
| | `HGET` | `HGET <key> <campo>` | Obtiene el valor del campo. |
| | `HDEL` | `HDEL <key> <campo>` | Elimina un campo del hash. |
| | `HEXISTS`| `HEXISTS <key> <campo>` | Comprueba si existe el campo. |
| | `HLEN` | `HLEN <key>` | Retorna el número de campos del hash. |
| | `HGETALL`| `HGETALL <key>` | Retorna todos los pares campo/valor. |
| **Listas** | `LPUSH` | `LPUSH <key> <val1>...` | Inserta elementos al inicio de la lista. |
| | `RPUSH` | `RPUSH <key> <val1>...` | Inserta elementos al final de la lista. |
| | `LPOP` | `LPOP <key>` | Extrae y retorna el primer elemento. |
| | `RPOP` | `RPOP <key>` | Extrae y retorna el último elemento. |
| | `LLEN` | `LLEN <key>` | Retorna la longitud de la lista. |
| | `LRANGE`| `LRANGE <key> <ini> <fin>` | Rango de elementos (soporta -1). |
| **Claves/TTL**| `DEL` | `DEL <key1> [key2...]` | Borra una o más claves. |
| | `EXISTS`| `EXISTS <key>` | Verifica existencia (`1` o `0`). |
| | `EXPIRE`| `EXPIRE <key> <segundos>` | Define tiempo de vida (TTL). |
| | `TTL` | `TTL <key>` | Segundos restantes (`-1` sin TTL, `-2` inex). |
| | `DBSIZE`| `DBSIZE` | Total de claves en el motor. |
| | `FLUSHDB`| `FLUSHDB` | Vacía todos los datos y limpia AOF. |
| **Admin** | `BGREWRITEAOF` | `BGREWRITEAOF` | Compacta el archivo AOF en caliente. |
| | `INFO` | `INFO` | Métricas y telemetría de rendimiento. |
| | `AUTH` | `AUTH <password>` | Autentica ante `requirepass`. |
| | `PING` | `PING [msg]` | Comprueba latencia o hace eco. |
| | `QUIT` | `QUIT` | Cierra la conexión TCP limpiamente. |

---

## 💻 Ejemplos de Integración

### Con `redis-cli`:
```bash
$ redis-cli -p 8080
127.0.0.1:8080> HSET user:100 name "Carlos" role "Architect"
(integer) 1
127.0.0.1:8080> HGETALL user:100
1) "name"
2) "Carlos"
3) "role"
4) "Architect"
127.0.0.1:8080> RPUSH queue "job_1" "job_2"
(integer) 2
127.0.0.1:8080> LPOP queue
"job_1"
127.0.0.1:8080> INCR visitors
(integer) 1
127.0.0.1:8080> BGREWRITEAOF
Background append only file rewriting finished successfully
```

### Con Python (`redis-py`):
```python
import redis

client = redis.Redis(host='localhost', port=8080, decode_responses=True)

# Hashes
client.hset("device:1", mapping={"ip": "192.168.1.50", "status": "online"})
print(client.hgetall("device:1"))

# Contadores Atómicos
client.incr("page_views")
print("Vistas:", client.get("page_views"))

# Listas
client.rpush("tasks", "task_alpha", "task_beta")
print("Siguiente tarea:", client.lpop("tasks"))
```

---

## 🧪 Suite de Pruebas Integrales (9/9 Suites)

La suite de pruebas automatizada ([tests/test_concurrent.cpp](tests/test_concurrent.cpp)) cubre:
1. `test_basic_crud`: SET, GET, DEL, EXISTS, SIZE, CLEAR sobre Strings.
2. `test_atomic_counters`: INCR, DECR, INCRBY con aritmética segura y control de desbordamiento.
3. `test_hashes`: HSET, HGET, HDEL, HEXISTS, HLEN, HGETALL.
4. `test_lists`: LPUSH, RPUSH, LPOP, RPOP, LLEN, LRANGE con índices positivos y negativos.
5. `test_ttl_expiration`: Expiración pasiva temporal y purga activa en background.
6. `test_lru_eviction`: Expulsión precisa de las claves menos recientemente usadas.
7. `test_resp_protocol`: Validación bidireccional del protocolo RESP2 y tipos nativos.
8. `test_aof_persistence_and_rewrite`: Reducción drástica del tamaño del log tras compactación `BGREWRITEAOF` y reconstrucción íntegra de Strings, Hashes y Listas.
9. `test_high_concurrency_sharding`: 50.000 operaciones simultáneas sobre los 64 shards superando 150.000 ops/seg sin fallos de segmentación ni contención.

---

## 📄 Licencia

Este proyecto está bajo la Licencia MIT. Consulta el archivo [LICENSE](LICENSE) para más detalles.
