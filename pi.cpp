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

template <class Args>
void launch(std::vector<Args>& args, void* (*worker)(void*)) {
    std::vector<pthread_t> threads(args.size());
    std::size_t created = 0;
    for (; created < args.size(); ++created) {
        int code = pthread_create(&threads[created], nullptr, worker, &args[created]);

        if (code != 0) {
            std::cerr << "No se pudo crear un hilo: " << code << '\n';
            std::exit(EXIT_FAILURE);
        }
    }
    for (auto thread : threads) check(pthread_join(thread, nullptr));
}

std::pair<std::size_t, std::size_t> block(std::size_t n, std::size_t id,
                                        std::size_t threads) {
    const auto begin = (n / threads) * id + std::min(id, n % threads);
    return {begin, begin + n / threads + (id < n % threads ? 1 : 0)};
}

void timing(double serial, double parallel, std::size_t threads) {
    const double speedup = serial / parallel;
    std::cout << "Tiempo secuencial (s): " << serial
              << "\nTiempo Pthreads (s): " << parallel
              << "\nSpeedup: " << speedup
              << "\nEficiencia: " << speedup / static_cast<double>(threads) * 100.0 << "%\n";
}


double pi_sum(std::size_t begin, std::size_t end, std::size_t n) {
    double sum = 0.0;
    for (std::size_t i = begin; i < end; ++i) {
        const double x = (static_cast<double>(i) + 0.5) / static_cast<double>(n);
        sum += 4.0 / (1.0 + x * x);
    }
    return sum;
}

struct PiArgs {
    std::size_t begin, end, n;
    double sum = 0.0;
};

void* pi_worker(void* ptr) {
    auto& a = *static_cast<PiArgs*>(ptr);
    a.sum = pi_sum(a.begin, a.end, a.n);
    return nullptr;
}

bool run_pi(std::size_t n, std::size_t threads) {
    auto start = Clock::now();
    const double serial = pi_sum(0, n, n) / static_cast<double>(n);
    const double serial_time = seconds(start);
    std::vector<PiArgs> args;
    for (std::size_t id = 0; id < threads; ++id) {
        auto [begin, end] = block(n, id, threads);
        args.push_back({begin, end, n});
    }
    start = Clock::now();
    launch(args, pi_worker);
    double parallel = 0.0;
    // Solo el hilo principal reduce las sumas despues de los join.
    for (const auto& a : args) parallel += a.sum;
    parallel /= static_cast<double>(n);
    const double parallel_time = seconds(start);
    const double reference = std::acos(-1.0);
    const bool ok = std::abs(serial - parallel) <= 1e-10 * std::max(1.0, std::abs(serial));
    std::cout << "PI | intervalos=" << n << " | hilos=" << threads
              << "\nSecuencial: " << serial << "\nPthreads: " << parallel
              << "\nReferencia: " << reference
              << "\nError absoluto frente a pi: " << std::abs(parallel - reference) << '\n';
    timing(serial_time, parallel_time, threads);
    std::cout << "Coincidencia secuencial/paralela: " << (ok ? "OK" : "FALLO") << '\n';
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
    std::cout << "Uso: " << program << " INTERVALOS HILOS\n";
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
        const bool ok = run_pi(positive(argv[1]), positive(argv[2]));
        return ok ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
