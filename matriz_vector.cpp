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

// Reparte tambien el resto: los bloques difieren, como maximo, en un elemento.
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

// PARTE II: cada hilo escribe filas distintas del vector y.
struct MatrixArgs {
    const double* matrix;
    const double* x;
    double* y;
    std::size_t begin, end, columns;
};

void* matrix_worker(void* ptr) {
    const auto& a = *static_cast<MatrixArgs*>(ptr);
    for (std::size_t row = a.begin; row < a.end; ++row) {
        double sum = 0.0;
        for (std::size_t col = 0; col < a.columns; ++col)
            sum += a.matrix[row * a.columns + col] * a.x[col];
        a.y[row] = sum;
    }
    return nullptr;
}

bool run_matrix(std::size_t rows, std::size_t columns, std::size_t threads) {
    if (rows > std::vector<double>().max_size() / columns)
        throw std::runtime_error("Dimensiones de matriz demasiado grandes");
    std::vector<double> matrix(rows * columns), x(columns), serial(rows), parallel(rows);
    for (std::size_t i = 0; i < matrix.size(); ++i)
        matrix[i] = static_cast<double>(i % 101) / 101.0;
    for (std::size_t i = 0; i < columns; ++i) x[i] = static_cast<double>(i % 17) / 17.0;
    MatrixArgs sequential{matrix.data(), x.data(), serial.data(), 0, rows, columns};
    auto start = Clock::now();
    matrix_worker(&sequential);
    const double serial_time = seconds(start);
    std::vector<MatrixArgs> args;
    for (std::size_t id = 0; id < threads; ++id) {
        auto [begin, end] = block(rows, id, threads);
        args.push_back({matrix.data(), x.data(), parallel.data(), begin, end, columns});
    }
    start = Clock::now();
    launch(args, matrix_worker);
    const double parallel_time = seconds(start);
    double max_error = 0.0, scale = 1.0;
    for (std::size_t i = 0; i < rows; ++i) {
        max_error = std::max(max_error, std::abs(serial[i] - parallel[i]));
        scale = std::max(scale, std::abs(serial[i]));
    }
    const bool ok = max_error <= 1e-10 * scale;
    std::cout << "MATRIZ-VECTOR | filas=" << rows << " | columnas=" << columns
              << " | hilos=" << threads << "\nError maximo: " << max_error << '\n';
    timing(serial_time, parallel_time, threads);
    std::cout << "Verificacion: " << (ok ? "OK" : "FALLO") << '\n';
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
    std::cout << "Uso: " << program << " FILAS COLUMNAS HILOS\n";
}

int main(int argc, char** argv) {
    std::cout << std::setprecision(15);
    try {
        if (argc == 2 && std::string(argv[1]) == "--help") {
            usage(argv[0]);
            return 0;
        }
        if (argc != 4) {
            usage(argv[0]);
            return 1;
        }
        const bool ok = run_matrix(positive(argv[1]), positive(argv[2]), positive(argv[3]));
        return ok ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
