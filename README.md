
# Laboratorio 5: implementación con Pthreads

## Entorno declarado por el estudiante

- Sistema operativo: **Pop!_OS 24.04 LTS x86_64**.
- Kernel: `7.0.11-76070011-generic`.
- Equipo: HP 250 G8 Notebook PC.
- Procesador: Intel Core i5-1035G1, 4 núcleos físicos y 8 procesadores lógicos; frecuencia indicada por neofetch: 3.600 GHz.
- Memoria RAM: aproximadamente 16 GiB (15771 MiB indicados por neofetch).
- Escritorio: COSMIC. Shell: Bash 5.2.21.
- GPU: Intel Iris Plus Graphics G1 y NVIDIA GeForce MX130. Estos programas utilizan la CPU.

Los datos anteriores corresponden a la salida compartida por el estudiante. Registrar también `g++ --version` al realizar los experimentos.

## Compilación y ejecución

Desde la raíz del proyecto:

```bash
cd lab5
make
# Compilar solo un ejercicio:
make pi
# Alternativa sin make:
g++ -std=c++17 -O2 -Wall -Wextra -Wpedantic pi.cpp -o pi -pthread

./pi 10000000 4
./matriz_vector 2000 2000 4
./lista 100000 4 mutex
./lista 100000 4 sin-sync
./barreras 1000 4
```

Se requieren `g++` y `make`. Cada ejecutable admite `--help` para mostrar sus argumentos. Todos los tamaños y cantidades de hilos deben ser positivos. El código de salida es 0 si la verificación pasa, 2 si falla y 1 ante argumentos o errores de ejecución. La lista sin sincronización puede fallar intencionalmente.

## Archivos independientes

- `pi.cpp`: cálculo de π, versiones secuencial y Pthreads.
- `matriz_vector.cpp`: multiplicación matriz-vector, versiones secuencial y Pthreads.
- `lista.cpp`: lista enlazada con mutex y sin sincronización.
- `barreras.cpp`: las tres implementaciones de barreras reutilizables.

Cada archivo contiene su propio `main()` y se compila por separado; no se deben enlazar todos los `.cpp` en un único ejecutable. `main.cpp` fue reemplazado por estos cuatro programas. Las opciones de compilación mostradas para `pi.cpp` también sirven para los otros archivos, cambiando el nombre de entrada y salida.

Tu versión previa con MPI se conserva en `pi_mpi.cpp`, fuera de los objetivos del Makefile porque este laboratorio utiliza Pthreads. El ejecutable antiguo `laboratorio` corresponde a la versión unificada anterior; `make` genera los cuatro programas separados.

## Estrategias implementadas

1. **π:** integración por puntos medios de `4/(1+x²)` entre 0 y 1. Los hilos reciben bloques de iteraciones equilibrados y acumulan sumas privadas. El principal combina las sumas después de `pthread_join`, sin mutex. Se muestran las aproximaciones secuencial y paralela, el error respecto de `acos(-1)` y la coincidencia entre versiones con tolerancia numérica.
2. **Matriz-vector:** datos deterministas y matriz contigua por filas. Cada hilo escribe posiciones distintas del resultado, de modo que no requiere mutex. La inicialización y reserva de memoria quedan fuera de la medición. Se comprueba el vector completo frente al secuencial.
3. **Lista enlazada:** cada hilo inserta un conjunto distinto de nodos en una cabeza compartida. El modo `mutex` protege la lectura de la cabeza, el enlace del nodo y la actualización de la cabeza. La inspección final cuenta los nodos y detecta repetidos/ciclos. El orden de inserción varía entre ejecuciones. Los nodos se reservan antes del trabajo y se liberan juntos después del join, incluidos los que puedan perderse de la cadena.
4. **Barreras reutilizables:** espera activa con contadores atómicos y generación; semáforos con dos torniquetes y contador protegido por mutex; y mutex con variable de condición y generación. Cada fase lleva su propio contador atómico de llegadas: después de la barrera se comprueba que todos los participantes hayan llegado. Se registran violaciones y tiempo por fase. Las generaciones y los dos torniquetes evitan mezclar ciclos sucesivos.

El PDF no identifica el libro del «Capítulo 4». Las tres alternativas anteriores son la selección de esta implementación; contrastarlas con el material del curso si allí se especifican variantes concretas.

### Experimento de lista sin sincronización

`sin-sync` omite deliberadamente el mutex. Dos hilos pueden leer la misma cabeza y sobrescribir la inserción del otro. En C++ este acceso concurrente constituye una **data race y produce comportamiento indefinido**: perder nodos es una manifestación posible, pero también podría parecer correcto o terminar anormalmente. No se garantiza que falle en cada ejecución. Se incluye únicamente para la comparación que pide el laboratorio; el modo correcto es `mutex`.

## Mediciones para la siguiente etapa

Los tiempos de π y matriz incluyen creación de hilos, cómputo y join; π incluye también la reducción final. El speedup es `T_secuencial / T_paralelo` y la eficiencia es `speedup / hilos`, expresada en porcentaje. Las barreras incluyen los contadores de verificación y el arranque/finalización de hilos, por lo que el tiempo medio por fase corresponde al experimento completo, no al costo aislado de la primitiva.

Para explorar 1, 2, 4 y 8 hilos:

```bash
for h in 1 2 4 8; do
    ./pi 10000000 "$h"
    ./matriz_vector 2000 2000 "$h"
    ./lista 100000 "$h" mutex
    ./barreras 1000 "$h"
done
```
