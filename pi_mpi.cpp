#include <mpi.h>
#include <iostream>
#include <iomanip>

int main(int argc, char *argv[]) {
    int rank, size;
    long long n = 5000000000LL;
    double sum = 0.0, step, x, pi;

    MPI_Init(&argc, &argv);
    MPI_Comm_rank(MPI_COMM_WORLD, &rank);
    MPI_Comm_size(MPI_COMM_WORLD, &size);

    step = 1.0 / static_cast<double>(n);

    for (long long i = rank; i < n; i += size) {
        x = (i + 0.5) * step;
        sum += 4.0 / (1.0 + x * x);
    }
    double my_pi = sum * step;

    MPI_Reduce(&my_pi, &pi, 1, MPI_DOUBLE, MPI_SUM, 0, MPI_COMM_WORLD);

    if (rank == 0) {
        std::cout << std::fixed << std::setprecision(16);
        std::cout << "Calculo completado. Pi: " << pi << " con " << size << " procesos." << std::endl;
    }

    MPI_Finalize();
    return 0;
}
