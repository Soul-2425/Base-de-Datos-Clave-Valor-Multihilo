#pragma once

#include <vector>
#include <queue>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <future>
#include <functional>
#include <type_traits>
#include <atomic>
#include <memory>
#include <stdexcept>

/**
 * @class ThreadPool
 * @brief Administrador de hilos concurrente con cola de tareas protegida.
 * 
 * DECISIONES DE SINCRONIZACIÓN Y MODELO DE MEMORIA:
 * 1. Cola de Trabajo y std::condition_variable:
 *    - El pool mantiene una cantidad fija de hilos trabajadores (std::vector<std::thread>)
 *      que duermen pasivamente en una variable de condición (cv_.wait()).
 *    - Esto evita el consumo innecesario de ciclos de CPU por 'busy-waiting' o sondeo (spinlocks),
 *      cediendo el control al planificador del sistema operativo cuando la cola está vacía.
 * 
 * 2. Protección Crítica y Desacoplamiento de Ejecución (Lock-Elision durante la tarea):
 *    - La cola std::queue<std::function<void()>> no es thread-safe por naturaleza.
 *    - Se utiliza std::mutex para sincronizar la extracción (pop) e inserción (push) de tareas.
 *    - DECISIÓN DE RENDIMIENTO CRÍTICA: Un hilo trabajador adquiere el cerrojo (lock), extrae la
 *      tarea de la cola, y **libera el cerrojo INMEDIATAMENTE ANTES de invocar la tarea**.
 *      Si la tarea se ejecutara manteniendo el mutex adquirido, el Thread Pool se degradaría a una
 *      ejecución estrictamente serializada (un hilo a la vez), eliminando cualquier beneficio de concurrencia.
 * 
 * 3. Notificación Eficiente:
 *    - Al encolar una nueva tarea, se emite cv_.notify_one(), despertando únicamente a un hilo durmiente
 *      y evitando el fenómeno de "rebaño atronador" (thundering herd problem).
 * 
 * 4. Apagado Limpio y RAII (Graceful Shutdown):
 *    - El destructor activa stop_ = true y realiza cv_.notify_all().
 *    - Los hilos procesan las tareas restantes pendientes en la cola antes de salir limpiamente.
 *    - Se ejecuta join() sobre cada hilo en el vector para garantizar que ningún recurso del sistema
 *      operativo quede huérfano ni se produzcan accesos a memoria destruida (std::terminate).
 */
class ThreadPool {
public:
    /**
     * @brief Construye el ThreadPool con una cantidad fija de hilos.
     * @param num_threads Cantidad de hilos trabajadores. Por defecto usa std::thread::hardware_concurrency().
     */
    explicit ThreadPool(std::size_t num_threads = std::thread::hardware_concurrency());

    /**
     * @brief Destructor RAII que asegura el drenado y finalización de los hilos de trabajo.
     */
    ~ThreadPool();

    // No copiable ni asignable
    ThreadPool(const ThreadPool&) = delete;
    ThreadPool& operator=(const ThreadPool&) = delete;

    ThreadPool(ThreadPool&&) = delete;
    ThreadPool& operator=(ThreadPool&&) = delete;

    /**
     * @brief Encola una tarea genérica y devuelve un std::future con su resultado.
     * @tparam F Tipo del invocable (función, lambda, bind).
     * @tparam Args Tipos de los argumentos.
     * @param f Función a ejecutar en un hilo trabajador.
     * @param args Argumentos a pasar a la función.
     * @return std::future con el resultado de la función.
     */
    template <class F, class... Args>
    auto enqueue(F&& f, Args&&... args) 
        -> std::future<typename std::invoke_result<F, Args...>::type> {
        
        using return_type = typename std::invoke_result<F, Args...>::type;

        // Se encapsula la tarea en un std::packaged_task para capturar el valor de retorno o excepción
        auto task = std::make_shared<std::packaged_task<return_type()>>(
            [func = std::forward<F>(f), 
             capture_args = std::make_tuple(std::forward<Args>(args)...)]() mutable {
                return std::apply(std::move(func), std::move(capture_args));
            }
        );

        std::future<return_type> res = task->get_future();
        {
            std::unique_lock<std::mutex> lock(queue_mutex_);

            if (stop_.load(std::memory_order_acquire)) {
                throw std::runtime_error("ThreadPool::enqueue llamado en un pool detenido");
            }

            // Encolamos un wrapper void() que invoca la packaged_task
            tasks_.emplace([task]() { (*task)(); });
        }

        // Despertar a un único trabajador para procesar la nueva tarea
        cv_.notify_one();
        return res;
    }

    /**
     * @brief Encola una tarea simple tipo "fire-and-forget" sin sobrecarga de std::future.
     * @param task Función invocable std::function<void()>.
     */
    void enqueue_detach(std::function<void()> task);

    /**
     * @brief Retorna la cantidad de hilos trabajadores activos en el pool.
     */
    [[nodiscard]] std::size_t thread_count() const noexcept {
        return workers_.size();
    }

    /**
     * @brief Comprueba si el pool ha sido detenido.
     */
    [[nodiscard]] bool is_stopped() const noexcept {
        return stop_.load(std::memory_order_acquire);
    }

private:
    // Hilos de trabajo del pool
    std::vector<std::thread> workers_;

    // Cola de tareas invocables
    std::queue<std::function<void()>> tasks_;

    // Sincronización de acceso a la cola de tareas
    mutable std::mutex queue_mutex_;

    // Variable de condición para notificar la disponibilidad de nuevas tareas
    std::condition_variable cv_;

    // Bandera atómica de terminación del pool
    std::atomic<bool> stop_{false};
};
