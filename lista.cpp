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

// PARTE III: insercion en cabeza. Los nodos se reservan antes de medir.
// No se eliminan nodos concurrentemente: cada uno tiene un propietario unico.
struct Node { std::size_t value = 0; Node* next = nullptr; };
struct ListArgs {
    Node** head;
    Node* nodes;
    std::size_t begin, end;
    pthread_mutex_t* mutex;
    bool synchronized;
};

void* list_worker(void* ptr) {
    auto& a = *static_cast<ListArgs*>(ptr);
    for (std::size_t i = a.begin; i < a.end; ++i) {
        if (a.synchronized) check(pthread_mutex_lock(a.mutex));
        // En modo sin sincronizacion estas lecturas/escrituras tienen una carrera.
        a.nodes[i].next = *a.head;
        *a.head = &a.nodes[i];
        if (a.synchronized) check(pthread_mutex_unlock(a.mutex));
    }
    return nullptr;
}

bool run_list(std::size_t n, std::size_t threads, bool synchronized) {
    auto nodes = std::make_unique<Node[]>(n);
    for (std::size_t i = 0; i < n; ++i) nodes[i].value = i;
    Node* head = nullptr;
    pthread_mutex_t mutex;
    check(pthread_mutex_init(&mutex, nullptr));
    std::vector<ListArgs> args;
    for (std::size_t id = 0; id < threads; ++id) {
        auto [begin, end] = block(n, id, threads);
        args.push_back({&head, nodes.get(), begin, end, &mutex, synchronized});
    }
    auto start = Clock::now();
    launch(args, list_worker);
    const double elapsed = seconds(start);
    // Inspeccion despues del join; detecta nodos ausentes, repetidos o ciclos.
    std::vector<bool> seen(n, false);
    std::size_t count = 0;
    bool valid = true;
    for (Node* node = head; node != nullptr; node = node->next) {
        if (node->value >= n || seen[node->value]) { valid = false; break; }
        seen[node->value] = true;
        ++count;
    }
    valid = valid && count == n;
    check(pthread_mutex_destroy(&mutex));
    std::cout << "LISTA | modo=" << (synchronized ? "mutex" : "sin sincronizacion")
              << " | hilos=" << threads << "\nInserciones esperadas: " << n
              << "\nNodos alcanzables: " << count << "\nTiempo (s): " << elapsed
              << "\nConsistencia: " << (valid ? "OK" : "FALLO") << '\n';
    return valid;
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
    std::cout << "Uso: " << program << " INSERCIONES HILOS mutex|sin-sync\n";
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
        const std::string mode(argv[3]);
        if (mode != "mutex" && mode != "sin-sync")
            throw std::runtime_error("Modo de lista: mutex o sin-sync");
        if (mode == "sin-sync")
            std::cout << "Experimento intencional con data race: comportamiento indefinido en C++.\n"
                      << "Un resultado OK no demuestra ausencia de carreras.\n";
        const bool ok = run_list(positive(argv[1]), positive(argv[2]), mode == "mutex");
        return ok ? 0 : 2;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
