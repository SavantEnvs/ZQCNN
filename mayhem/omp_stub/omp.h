// Single-threaded stand-in for <omp.h>: the base image has clang but no libomp. Used with
// -I<this dir> and without -fopenmp, so every `#pragma omp` is ignored and the code runs serially
// (deterministic for fuzzing).
#ifndef MAYHEM_OMP_STUB_H_
#define MAYHEM_OMP_STUB_H_
#include <time.h>
#ifdef __cplusplus
extern "C" {
#endif
static inline int omp_get_thread_num(void) { return 0; }
static inline int omp_get_num_threads(void) { return 1; }
static inline int omp_get_max_threads(void) { return 1; }
static inline int omp_get_num_procs(void) { return 1; }
static inline void omp_set_num_threads(int n) { (void)n; }
static inline double omp_get_wtime(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (double)ts.tv_sec + 1e-9 * (double)ts.tv_nsec;
}
#ifdef __cplusplus
}
#endif
#endif
