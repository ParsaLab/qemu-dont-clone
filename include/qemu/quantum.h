#ifndef QEMU_QUANTUM_H
#define QEMU_QUANTUM_H

#include <stdatomic.h>
#include <stdint.h>

typedef enum thread_quamtum_state_t {
    QUANTUM_STATE_RUNNING,      // the thread is running
    QUANTUM_STATE_SUSPENDED,    // the thread is suspended. It may leave the quantum barrier for a reason.
    QUANTUM_STATE_SYNCHRONIZING,// the thread is synchronizing. It cannot leave the quantum barrier and it has depleted its credit.
} thread_quamtum_state_t;

typedef struct quamtum_per_thread_data_t {
    uint64_t ip10ps;  // the number of instruction per 10ps, used to convert the credit unit between time and instruction.
    int64_t credit; // in the number of instructions. 
    int64_t credit_in_time; // in the unit of time. This member is only calculated before a suspended thread enters the quantum barrier. 
    uint64_t most_recent_deadline_in_virtual_time; // the most recent deadline in QEMU virtual time. This member is only calculated before a suspended thread enters the quantum barrier.
    uint64_t generation;                           // which generation the thread is in.
    thread_quamtum_state_t state;                  // the state of the thread. 
} quamtum_per_thread_data_t;

typedef struct quantum_barrier_t {
    _Atomic(uint64_t) lock; // atomic, 0 means unlocked, 1 means locked.

    uint64_t suspended_thread_count; // updated by each thread
    uint64_t synchronizing_thread_count; // updated by each thread

    _Atomic(uint64_t) last_thread_entered; // atomic, updated only by the last thread. Threads in the suspended mode will transfer to the synchronizing mode when the last thread is entered.
    _Atomic(uint64_t) quantum_resolution_result; // This suggests whether the past quantum is a complete one or not. 1 means complete, 0 means incomplete.

    uint64_t threshold;  // the number of threads managedee by the quantum barrier.

    uint64_t generation; // the current generation of the quantum barrier.
    uint64_t passed_time; // the time passed in the current quantum, in nanoseconds.

    struct quamtum_per_thread_data_t *per_thread_data[256]; // indexed by the CPU index. No larger than the threshold.

    // Callback related members.
    uint64_t next_check_threshold; // the next time to check the threshold.
} quantum_barrier_t;


extern quantum_barrier_t quantum_barrier;

void quantum_barrier_init(quantum_barrier_t *barrier);

void quantum_barrier_lock(quantum_barrier_t *barrier);

void quantum_barrier_unlock(quantum_barrier_t *barrier);

void quantum_barrier_register(quantum_barrier_t *barrier, uint64_t cpu_index, quamtum_per_thread_data_t *per_thread_data);

typedef enum quantum_barrier_resolution_state_t {
    QUANTUM_COMPLETE,  // at least one thread ends up in the synchronizing mode.
    QUANTUM_INCOMPLETE, // all threads are in the suspended mode.
    QUANTUM_CONTINUE, // the thread is in the suspended mode and it should continue.
} quantum_barrier_synchronization_result_t;

typedef struct quantum_barrier_resolution_result_t {
    enum quantum_barrier_resolution_state_t quantum_resolution_result;
    uint64_t generation;                                                // the new generation
} quantum_barrier_resolution_result_t;

void quantum_barrier_wait(quantum_barrier_t *barrier, uint64_t is_suspended, quantum_barrier_resolution_result_t *result);

typedef struct ipi_time_adjustment_request_t {
    uint64_t is_valid;
    uint64_t in_which_generation;
    uint64_t remaining_credit_in_time;
} ipi_time_adjustment_request_t;

#endif /* QEMU_QUANTUM_H */