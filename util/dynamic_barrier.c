#include "qemu/dynamic_barrier.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

#include "qemu/osdep.h"
#include "sysemu/runstate.h"
#include "sysemu/quantum.h"
#include "qemu/plugin-cyan.h"



static uint64_t get_current_timestamp_ns(void) {
    struct timespec ts;
    // Get the current time
    clock_gettime(CLOCK_REALTIME, &ts);

    // Convert to nanoseconds
    // tv_sec is seconds, tv_nsec is nanoseconds
    uint64_t timestamp_ns = (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;

    return timestamp_ns;
}

static void *report_time_peridically(void *arg) {
    dynamic_barrier_polling_t *barrier = arg;
    while (1) {
        sleep(10);
        uint64_t total_diff = barrier->total_diff;
        uint64_t generation = barrier->generation;
        printf("Total time spent in the barrier: %lu ns, generation: %lu, normalized_diff: %lf\n", total_diff, generation, (double)total_diff / generation);
    }
    return NULL;
}

// create a timestamp of each thread. 
// static __thread uint64_t thread_start_quantum_timestamp = 0;

// // Initialize the dynamic barrier
// int dynamic_barrier_init(dynamic_barrier_t *barrier, int initial_threshold) {
//     int status;

//     status = pthread_mutex_init(&barrier->mutex, NULL);
//     if (status != 0) return status;

//     status = pthread_cond_init(&barrier->cond, NULL);
//     if (status != 0) {
//         pthread_mutex_destroy(&barrier->mutex);
//         return status;
//     }

//     barrier->threshold = initial_threshold;
//     barrier->count = 0;
//     barrier->generation = 0;

//     // start another thread to call report_time_peridically.

//     return 0;
// }

// // Destroy the dynamic barrier
// int dynamic_barrier_destroy(dynamic_barrier_t *barrier) {
//     pthread_mutex_destroy(&barrier->mutex);
//     pthread_cond_destroy(&barrier->cond);
//     return 0;
// }

// // Wait on the barrier
// int dynamic_barrier_wait(dynamic_barrier_t *barrier) {
//     pthread_mutex_lock(&barrier->mutex);

//     int gen = barrier->generation;
//     barrier->count++;

//     if (barrier->count >= barrier->threshold) {
//         barrier->generation++;
//         barrier->count = 0;
//         pthread_cond_broadcast(&barrier->cond);
//     } else {
//         while (gen == barrier->generation) {
//             pthread_cond_wait(&barrier->cond, &barrier->mutex);
//         }
//     }

//     pthread_mutex_unlock(&barrier->mutex);
//     return 0;
// }



// int dynamic_barrier_increase_by_1(dynamic_barrier_t *barrier) {
//     pthread_mutex_lock(&barrier->mutex);
//     barrier->threshold = barrier->threshold + 1;
//     if (barrier->count >= barrier->threshold) {
//         barrier->generation++;
//         barrier->count = 0;
//         pthread_cond_broadcast(&barrier->cond);
//     }
//     pthread_mutex_unlock(&barrier->mutex);
//     return 0;
// }

// int dynamic_barrier_decrease_by_1(dynamic_barrier_t *barrier) {
//     pthread_mutex_lock(&barrier->mutex);
//     if (barrier->threshold <= 0) {
//         pthread_mutex_unlock(&barrier->mutex);
//         return -1;
//     }
//     barrier->threshold = barrier->threshold - 1;
//     if (barrier->count >= barrier->threshold) {
//         barrier->generation++;
//         barrier->count = 0;
//         pthread_cond_broadcast(&barrier->cond);
//     }
//     pthread_mutex_unlock(&barrier->mutex);
//     return 0;
// }


int dynamic_barrier_polling_init(dynamic_barrier_polling_t *barrier, int initial_threshold) {
    barrier->lock.next_ticket = 0;
    barrier->lock.now_serving = 0;

    barrier->threshold = initial_threshold;
    barrier->count = 0;
    barrier->generation = 0;

    if (quantum_enabled()) {
        // pthread_t tid;
        // pthread_create(&tid, NULL, report_time_peridically, barrier);
    }

    for (int i = 0; i < 128; i++) {
        barrier->histogram[i] = create_histogram(100, 1e5, 101e5);
    }

    barrier->current_cycle = 0;
    barrier->next_check_threshold = quantum_check_threshold;
    
    return 0;
}

int dynamic_barrier_polling_destroy(dynamic_barrier_polling_t *barrier) {
    for (int i = 0; i < 128; i++) {
        free_histogram(barrier->histogram[i]);
    }
    
    return 0;
}

static void dynamic_barrier_polling_acquire_lock(dynamic_barrier_polling_t *barrier) {
    uint64_t my_ticket = atomic_fetch_add(&barrier->lock.next_ticket, 1);
    while (atomic_load(&barrier->lock.now_serving) != my_ticket) {
        // do nothing
    }
}

static void dynamic_barrier_polling_release_lock(dynamic_barrier_polling_t *barrier) {
    atomic_fetch_add(&barrier->lock.now_serving, 1);
}

uint32_t dynamic_barrier_polling_wait(dynamic_barrier_polling_t *barrier, uint32_t private_generation, bool *stop_request) {
    dynamic_barrier_polling_acquire_lock(barrier);

    uint64_t current_gen = atomic_load(&barrier->generation);

    assert(private_generation == current_gen);

    uint64_t waiting_count = barrier->count; 
    
    if (waiting_count == barrier->threshold - 1) {
        barrier->current_cycle += quantum_size;
        barrier->stop_request = 0;

        if (barrier->next_check_threshold != 0 && barrier->current_cycle >= barrier->next_check_threshold) {
            if (cyan_periodic_check_cb != NULL) {
                if(cyan_periodic_check_cb(quantum_check_threshold)) {
                    barrier->stop_request = true;
                    // wait for the machine state to become suspended for VM.
                    while (runstate_get() != RUN_STATE_SAVE_VM) {

                    }
                }
            } 
            barrier->next_check_threshold += quantum_check_threshold;
        }

        barrier->count = 0;

        // The last core will synchronize the time to all cores.
        // Find the maximum vtime.
        uint64_t max_vtime = 0;
        for (int i = 0; i < barrier->threshold; i++) {
            uint64_t vtime = cpu_virtual_time[i].vts;
            if (vtime > max_vtime) {
                max_vtime = vtime;
            }
        }

        // Synchronize the time.
        for (int i = 0; i < barrier->threshold; i++) {
            // assert(cpu_virtual_time[i].vts <= max_vtime);
            if (cpu_virtual_time[i].vts > max_vtime) {
                printf("Thread %d, vts: %lu, max_vtime: %lu\n", i, cpu_virtual_time[i].vts, max_vtime);
                assert(false);
            }

            cpu_virtual_time[i].vts = max_vtime;
        }

        // increase the generation and notify others.
        atomic_fetch_add(&barrier->generation, 1);

        dynamic_barrier_polling_release_lock(barrier); // we can release the generation here.

        *stop_request = barrier->stop_request;
    } else {
        barrier->count += 1;
        bool require_stop = barrier->stop_request;
        dynamic_barrier_polling_release_lock(barrier);

        // You just need to wait.
        while (atomic_load(&barrier->generation) == private_generation) {
            // do nothing, because the current generation is not changed.
        }

        *stop_request = require_stop;
    }

    return current_gen + 1;
}

uint32_t dynamic_barrier_polling_increase_by_1(dynamic_barrier_polling_t *barrier) {
    uint32_t current_generation;
    dynamic_barrier_polling_acquire_lock(barrier);
    current_generation = atomic_load(&barrier->generation);
    barrier->threshold += 1;
    dynamic_barrier_polling_release_lock(barrier);

    return current_generation;
}

int dynamic_barrier_polling_decrease_by_1(dynamic_barrier_polling_t *barrier) {
    dynamic_barrier_polling_acquire_lock(barrier);
    if (barrier->threshold <= 0) {
        dynamic_barrier_polling_release_lock(barrier);
        assert(false);
    }

    barrier->threshold -= 1;
    uint64_t waiting_count = barrier->count;

    if (waiting_count == barrier->threshold && waiting_count != 0) {
        barrier->current_cycle += quantum_size;

        if (barrier->next_check_threshold != 0 && barrier->current_cycle >= barrier->next_check_threshold) {
            if (cyan_periodic_check_cb != NULL) cyan_periodic_check_cb(quantum_check_threshold);
            barrier->next_check_threshold += quantum_check_threshold;
        }


        barrier->count = 0;

        // increase the generation and notify others.
        atomic_fetch_add(&barrier->generation, 1);
    }

    dynamic_barrier_polling_release_lock(barrier);
    return 0;
}

void dynamic_barrier_polling_reset(dynamic_barrier_polling_t *barrier) {
    dynamic_barrier_polling_acquire_lock(barrier);
    atomic_store(&barrier->generation, 0); // this should make everyone to not wait. 
    barrier->count = 0;
    dynamic_barrier_polling_release_lock(barrier);
}