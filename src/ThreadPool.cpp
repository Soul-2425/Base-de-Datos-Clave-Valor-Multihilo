#include "ThreadPool.hpp"

ThreadPool::ThreadPool(std::size_t num_threads) {
    // Si hardware_concurrency() devuelve 0 (no detectable), asignamos al menos 2 hilos
    if (num_threads == 0) {
        num_threads = 2;
    }

    workers_.reserve(num_threads);

    for (std::size_t i = 0; i < num_threads; ++i) {
        workers_.emplace_back([this]() {
            while (true) {
                std::function<void()> task;

                {
                    // Adquirimos el lock sobre la cola de tareas
                    std::unique_lock<std::mutex> lock(this->queue_mutex_);

                    // Ponemos al hilo a dormir hasta que haya trabajo disponible o el pool sea detenido
                    this->cv_.wait(lock, [this]() {
                        return this->stop_.load(std::memory_order_acquire) || !this->tasks_.empty();
                    });

                    // Si el pool fue detenido y la cola se ha drenado completamente, el hilo sale
                    if (this->stop_.load(std::memory_order_acquire) && this->tasks_.empty()) {
                        return;
                    }

                    // Extraemos la siguiente tarea de la cola
                    task = std::move(this->tasks_.front());
                    this->tasks_.pop();

                } // <--- ¡AQUÍ SE LIBERA EL MUTEX! (Fin del bloque)
                  // Crucial: La tarea se ejecuta fuera del scope del lock, permitiendo
                  // que otros hilos adquieran el mutex simultáneamente para desencolar.

                // Ejecutamos la tarea de forma concurrente
                if (task) {
                    try {
                        task();
                    } catch (...) {
                        // Se previene que excepciones no capturadas en tareas destruyan el hilo trabajador
                    }
                }
            }
        });
    }
}

void ThreadPool::enqueue_detach(std::function<void()> task) {
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);

        if (stop_.load(std::memory_order_acquire)) {
            throw std::runtime_error("ThreadPool::enqueue_detach llamado en un pool detenido");
        }

        tasks_.push(std::move(task));
    }

    // Notificamos a un hilo durmiente
    cv_.notify_one();
}

ThreadPool::~ThreadPool() {
    // Señalizamos el cese de operaciones de forma atómica con semántica release
    stop_.store(true, std::memory_order_release);

    // Despertamos a todos los hilos durmientes para que procesen las tareas remanentes y salgan
    cv_.notify_all();

    // Esperamos a que todos los hilos de trabajo concluyan limpiamente (RAII)
    for (std::thread& worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}
