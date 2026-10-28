#include <pthread.h>
#include <semaphore.h>

#include <algorithm>
#include <atomic>
#include <cerrno>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iomanip>
#include <iostream>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

using Clock = std::chrono::steady_clock;

void check(int code) {
    if (code != 0) throw std::runtime_error("Error de Pthreads: " + std::to_string(code));
}

double seconds(Clock::time_point start) {
    return std::chrono::duration<double>(Clock::now() - start).count();
}

// Los argumentos se construyen antes de crear hilos y viven hasta el ultimo join.
template <class Args>
void launch(std::vector<Args>& args, void* (*worker)(void*)) {
    std::vector<pthread_t> threads(args.size());
    std::size_t created = 0;
    for (; created < args.size(); ++created) {
        int code = pthread_create(&threads[created], nullptr, worker, &args[created]);
        // Los trabajadores de barreras necesitan a todos sus participantes.
        if (code != 0) {
            std::cerr << "No se pudo crear un hilo: " << code << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    for (auto thread : threads) check(pthread_join(thread, nullptr));
}

// PARTE IV: tres barreras reutilizables.
enum class BarrierKind { Spin, Semaphore, Condition };
class Barrier {
    BarrierKind kind;
    const std::size_t participants;
    std::size_t count = 0, generation = 0;
    pthread_mutex_t mutex;
    pthread_cond_t condition;
    std::atomic<std::size_t> spin_count{0}, spin_generation{0};
    sem_t gate1, gate2;

    static void wait_sem(sem_t* sem) {
        while (sem_wait(sem) != 0) {
            if (errno != EINTR) std::abort();
        }
    }
    static void post_sem(sem_t* sem) {
        if (sem_post(sem) != 0) std::abort();
    }

public:
    Barrier(BarrierKind type, std::size_t n) : kind(type), participants(n) {
        check(pthread_mutex_init(&mutex, nullptr));
        check(pthread_cond_init(&condition, nullptr));
        if (sem_init(&gate1, 0, 0) != 0 || sem_init(&gate2, 0, 1) != 0)
            throw std::runtime_error("No se pudieron inicializar los semaforos");
    }
    ~Barrier() {
        sem_destroy(&gate1);
        sem_destroy(&gate2);
        pthread_cond_destroy(&condition);
        pthread_mutex_destroy(&mutex);
    }
    void wait() {
        if (kind == BarrierKind::Spin) {
            const auto current = spin_generation.load();
            if (spin_count.fetch_add(1) + 1 == participants) {
                spin_count.store(0);
                spin_generation.fetch_add(1);
            } else {
                while (spin_generation.load() == current) { /* espera activa */ }
            }
        } else if (kind == BarrierKind::Condition) {
            check(pthread_mutex_lock(&mutex));
            const auto current = generation;
            if (++count == participants) {
                count = 0;
                ++generation;
                check(pthread_cond_broadcast(&condition));
            } else {
                // El bucle tambien contempla despertares espurios.
                while (generation == current) check(pthread_cond_wait(&condition, &mutex));
            }
            check(pthread_mutex_unlock(&mutex));
        } else {
            // Dos torniquetes impiden mezclar participantes de ciclos distintos.
            check(pthread_mutex_lock(&mutex));
            if (++count == participants) {
                wait_sem(&gate2);
                post_sem(&gate1);
            }
            check(pthread_mutex_unlock(&mutex));
            wait_sem(&gate1);
            post_sem(&gate1);
            check(pthread_mutex_lock(&mutex));
            if (--count == 0) {
                wait_sem(&gate1);
                post_sem(&gate2);
            }
            check(pthread_mutex_unlock(&mutex));
            wait_sem(&gate2);
            post_sem(&gate2);
        }
    }
};

struct BarrierArgs {
    Barrier* barrier;
    std::atomic<std::size_t>* arrivals;
    std::size_t phases, participants, failures = 0;
};

void* barrier_worker(void* ptr) {
    auto& a = *static_cast<BarrierArgs*>(ptr);
    for (std::size_t phase = 0; phase < a.phases; ++phase) {
        a.arrivals[phase].fetch_add(1);
        a.barrier->wait();
        if (a.arrivals[phase].load() != a.participants) ++a.failures;
    }
    return nullptr;
}

bool run_barriers(std::size_t phases, std::size_t threads) {
    bool ok = true;
    for (auto kind : {BarrierKind::Spin, BarrierKind::Semaphore, BarrierKind::Condition}) {
        Barrier barrier(kind, threads);
        auto arrivals = std::make_unique<std::atomic<std::size_t>[]>(phases);
        for (std::size_t i = 0; i < phases; ++i) arrivals[i].store(0);
        std::vector<BarrierArgs> args(threads, {&barrier, arrivals.get(), phases, threads, 0});
        auto start = Clock::now();
        launch(args, barrier_worker);
        const double elapsed = seconds(start);
        std::size_t failures = 0;
        for (const auto& a : args) failures += a.failures;
        for (std::size_t i = 0; i < phases; ++i)
            if (arrivals[i].load() != threads) ++failures;
        const char* name = kind == BarrierKind::Spin ? "espera activa" :
                           kind == BarrierKind::Semaphore ? "semaforos" : "variable de condicion";
        std::cout << "BARRERA | " << name << " | hilos=" << threads << " | fases=" << phases
                  << "\nTiempo (s): " << elapsed
                  << "\nTiempo medio por fase (us): " << elapsed * 1e6 / static_cast<double>(phases)
                  << "\nViolaciones: " << failures << '\n';
        ok = ok && failures == 0;
    }
    return ok;
}

std::size_t positive(const char* text) {
    std::string value(text);
    if (value.empty() || value.find_first_not_of("0123456789") != std::string::npos)
        throw std::runtime_error("Se esperaba un entero positivo: " + value);
    const auto n = std::stoull(value);
    if (n == 0 || n > std::numeric_limits<std::size_t>::max())
        throw std::runtime_error("Numero fuera de rango: " + value);
    return static_cast<std::size_t>(n);
}

void usage(const char* program) {
    std::cout << "Uso: " << program << " FASES HILOS\n";
}

int main(int argc, char** argv) {
    std::cout << std::setprecision(15);
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (argc != 3) {
            usage(argv[0]);
            return 1;
        }
        const bool ok = run_barriers(positive(argv[1]), positive(argv[2]));
        return ok ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
