#ifndef QEMU_QUANTUM_H
#define QEMU_QUANTUM_H

#include <stdatomic.h>
#include <stdint.h>

typedef enum thread_quamtum_state_t {
    QUANTUM_STATE_RUNNING,      // the thread is running
    QUANTUM_STATE_SUSPENDED,    // the thread is suspended. It may leave the quantum barrier for a reason.
    QUANTUM_STATE_SYNCHRONIZING,// the thread is synchronizing. It cannot leave the quantum barrier and it has depleted its credit.
} thread_quantum_state_t;

typedef struct quamtum_per_thread_data_t {
    uint64_t ip100ns;  // the number of instruction per 10ps, used to convert the credit unit between time and instruction.
    int64_t credit; // in the number of instructions. 
    int64_t credit_in_10ps; // in the unit of time. This member is only calculated before a suspended thread enters the quantum barrier. 
    uint64_t generation;                           // which generation the thread is in.

    // The following two members are used by the TCG plugin.
    uint64_t credit_required; // the number of credit required to run the next quantum.
    uint64_t credit_depleted; // whether the credit is depleted or not.
} quantum_per_thread_data_t;


void quantum_barrier_init(void);

void quantum_barrier_lock(void);

void quantum_barrier_unlock(void);

void quantum_barrier_register(uint64_t cpu_index, quantum_per_thread_data_t *per_thread_data);

typedef enum quantum_barrier_resolution_state_t {
    QUANTUM_COMPLETE,  // at least one thread ends up in the synchronizing mode.
    QUANTUM_INCOMPLETE, // all threads are in the suspended mode.
    QUANTUM_CONTINUE, // the thread is in the suspended mode and it should continue.
} quantum_barrier_synchronization_result_t;

typedef struct quantum_barrier_resolution_result_t {
    enum quantum_barrier_resolution_state_t quantum_resolution_result;
    uint64_t generation;                                                // the new generation
} quantum_barrier_resolution_result_t;

void quantum_barrier_wait(uint64_t is_suspended, quantum_barrier_resolution_result_t *result);

// This function is called when the thread depletes its credit.
void quantum_recharge(int64_t upto, quantum_per_thread_data_t *per_thread_data);

// This function is called when the thread enters the suspended mode. 
void quantum_suspend(quantum_per_thread_data_t *per_thread_data);

typedef struct ipi_time_adjustment_request_t {
    uint64_t is_valid;
    uint64_t in_which_generation;
    uint64_t remaining_credit_in_time;
} ipi_time_adjustment_request_t;

#endif /* QEMU_QUANTUM_H */