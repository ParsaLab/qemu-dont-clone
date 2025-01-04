
#include "qemu/osdep.h"
#include "qemu/quantum.h"
#include "qemu/plugin-cyan.h"
#include "sysemu/quantum.h"
#include "hw/core/cpu.h"
#include "qemu/timer.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/cpus.h"

typedef struct quantum_barrier_t {
    _Atomic(uint64_t) lock; // atomic, 0 means unlocked, 1 means locked.

    // The following members are increased or decreased by each thread, and cleared by the last thread.
    uint64_t suspended_thread_count; // updated by each thread
    uint64_t synchronizing_thread_count; // updated by each thread

    // The following members are updated by the last thread with lock, but can be read by all threads without the lock
    _Atomic(uint64_t) last_thread_entered; // atomic, updated only by the last thread. Threads in the suspended mode will transfer to the synchronizing mode when the last thread is entered.
    _Atomic(uint64_t) quantum_resolution_result; // This suggests whether the past quantum is a complete one or not. 1 means complete, 0 means incomplete.
    _Atomic(uint64_t) generation; // the current generation of the quantum barrier.

    // The following members are updated by the last thread with lock, and can be read by all threads with the lock.
    uint64_t threshold;  // the number of threads managedee by the quantum barrier.
    uint64_t passed_time; // the time passed in the current quantum, in nanoseconds.
    uint64_t next_check_threshold; // the next time to check the threshold.

    struct quamtum_per_thread_data_t *per_thread_data[256]; // indexed by the CPU index. No larger than the threshold.
} quantum_barrier_t;


static quantum_barrier_t quantum_barrier;

void quantum_barrier_init(void) {
    atomic_store(&quantum_barrier.lock, 0);
    
    quantum_barrier.suspended_thread_count = 0;
    quantum_barrier.synchronizing_thread_count = 0;
    
    atomic_store(&quantum_barrier.last_thread_entered, 0);
    atomic_store(&quantum_barrier.quantum_resolution_result, 0);
    
    quantum_barrier.threshold = 0;
    quantum_barrier.generation = 0;
    quantum_barrier.passed_time = 0;

    for (int i = 0; i < 256; i++) {
        quantum_barrier.per_thread_data[i] = NULL;
    }

    quantum_barrier.next_check_threshold = 0;
}

void quantum_barrier_lock(void) {
    while (atomic_exchange(&quantum_barrier.lock, 1) == 1) {
        // do nothing
    }
}

void quantum_barrier_unlock(void) {
    atomic_store(&quantum_barrier.lock, 0);
}

void quantum_barrier_register(uint64_t cpu_index, quantum_per_thread_data_t *per_thread_data) {
    quantum_barrier_lock();

    assert(quantum_barrier.threshold < 256);
    quantum_barrier.per_thread_data[cpu_index] = per_thread_data;
    quantum_barrier.threshold += 1;

    quantum_barrier_unlock();
}

void quantum_barrier_wait(uint64_t is_suspended, quantum_barrier_resolution_result_t *result) {
    // First of all, get the lock of the quantum barrier. 
    quantum_barrier_lock();

    assert(quantum_barrier.suspended_thread_count >= 0);
    assert(quantum_barrier.synchronizing_thread_count >= 0);

    // Now, if the thread is suspended, increase the suspended_thread_count.
    if (is_suspended) {
        quantum_barrier.suspended_thread_count += 1;
    } else {
        quantum_barrier.synchronizing_thread_count += 1;
    }

    // Check whether the current quantum is a complete one.
    if (quantum_barrier.suspended_thread_count + quantum_barrier.synchronizing_thread_count == quantum_barrier.threshold) {
        // This is the last thread.
        quantum_barrier.last_thread_entered = 1; // cut off. Now all threads should be in the synchronization mode and cannot leave from the quantum barrier. 

        // Now, all threads are in the synchronizing mode. We need to calculate the target time based on all threads' local target time.

        // Now, we need to decide what to do. 
        uint64_t is_complete = quantum_barrier.synchronizing_thread_count > 0;
        if (is_complete) {
            // well, we don't have much thing to do. All threads clean their credit and go to the next quantum. 
            quantum_barrier.quantum_resolution_result = QUANTUM_COMPLETE;
            
            // update the barrier generation.
            quantum_barrier.generation += 1;
            // update the passed time.
            quantum_barrier.passed_time += quantum_size;

            quantum_barrier.last_thread_entered = 0; // this action will relax all synchronizing threads.
            
            // release the lock and the quantum barrier.
            quantum_barrier.suspended_thread_count = 0;
            quantum_barrier.synchronizing_thread_count = 0;
            quantum_barrier_unlock();

            // return. 
            result->quantum_resolution_result = QUANTUM_COMPLETE;
            result->generation = quantum_barrier.generation; 
            return;
        } else {
            quantum_barrier.quantum_resolution_result = QUANTUM_INCOMPLETE; // This is an incomplete quantum.

            // Now, we have a lot of trouble. All threads are in the suspended mode. 
            // 1. Suspend the advancement of the time. In this case, we can safely stop all threads without handling the timer interrupt outdated. 
            cpu_disable_ticks();

            // 2. Find the latest thread and know its remaining credit (in the unit of time.)
            uint64_t least_credit_in_time_10ps = 0xFFFFFFFFFFFFFFFF;
            for (int i = 0; i < quantum_barrier.threshold; i++) {
                if (quantum_barrier.per_thread_data[i]->credit_in_10ps < least_credit_in_time_10ps) {
                    least_credit_in_time_10ps = quantum_barrier.per_thread_data[i]->credit_in_10ps;
                }
            }

            // 3. Find the most recent deadline and calculate the amount of time that we should fast forward.
            // The most recent deadline is from the QEMU_CLOCK_VIRTUAL. 
            int64_t deadline_in_ns = qemu_clock_deadline_ns_all(QEMU_CLOCK_VIRTUAL, 0);
            assert(deadline_in_ns != -1);

            uint64_t current_vtime = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            assert(current_vtime != -1);

            int64_t time_to_fast_forward = deadline_in_ns - current_vtime;
            assert(time_to_fast_forward >= 0);

            // Run the fast forwarding.
            cpu_shift_vm_clock(time_to_fast_forward);

            int64_t remaining_credit_in_time = least_credit_in_time_10ps - time_to_fast_forward * 100;

            if (remaining_credit_in_time < 0) {
                // Well, the current quantum is deplete after fast forwarding. We need to go to the next quantum.
                while (remaining_credit_in_time < 0) {
                    remaining_credit_in_time += quantum_size * 100;
                    quantum_barrier.generation += 1;
                    quantum_barrier.passed_time += quantum_size;
                }
            } else {
                // The current quantum still has some credit after fast forwarding. We need to adjust the credit of each thread.
                quantum_barrier.passed_time += (quantum_size - remaining_credit_in_time / 100);
            }

            // 4. Recalculate the credit of each thread.
            for (int i = 0; i < quantum_barrier.threshold; i++) {
                quantum_barrier.per_thread_data[i]->credit_in_10ps = remaining_credit_in_time;
            }

            // 5. Resume the advancement of the time. 
            cpu_enable_ticks();

            quantum_barrier.last_thread_entered = 0; // this action will relax all synchronizing threads.

            // 6. release the lock and the quantum barrier.
            quantum_barrier.suspended_thread_count = 0;
            quantum_barrier.synchronizing_thread_count = 0;
            quantum_barrier_unlock();

            // No need to fill the quantum credit by each thread anymore. 
            result->quantum_resolution_result = QUANTUM_INCOMPLETE;
            result->generation = quantum_barrier.generation;

            return;
        }
    } else {
        // This is not the last thread. Relax the lock.
        quantum_barrier_unlock();

        // Depending on the is_suspended, wait. 
        if (is_suspended) {
            while (1) {
                if (cpu_can_run(current_cpu)) {
                    // The thread can run. It should go to another state. 
                    // Before doing that, we need to decrease the suspended_thread_count.
                    // Get the lock
                    quantum_barrier_lock();

                    if (quantum_barrier.generation != quantum_barrier.per_thread_data[current_cpu->cpu_index]->generation) {
                        // Well, this means that a quantum generation was resolved during when the thread tried to grab the lock. 
                        // The result of the latest quantum generation is stored in the quantum_barrier.
                        result->quantum_resolution_result = quantum_barrier.quantum_resolution_result;
                        result->generation = quantum_barrier.generation;
                        quantum_barrier_unlock();
                        return;
                    }

                    // Decrease the suspended_thread_count.
                    quantum_barrier.suspended_thread_count -= 1;

                    // Release the lock.
                    quantum_barrier_unlock();

                    // Time to leave.
                    result->quantum_resolution_result = QUANTUM_CONTINUE;

                    return;
                }

                // If the last thread has appeared during the waiting process, we need to go to the synchronizing mode.
                if (quantum_barrier.last_thread_entered) {
                    // We need to wait for the last thread to finish its job.
                    while(quantum_barrier.last_thread_entered) {
                        // do nothing
                    }

                    // Return, because the quantum is resolved. 
                    result->quantum_resolution_result = quantum_barrier.quantum_resolution_result;
                    result->generation = quantum_barrier.generation;
                    return;
                }
            }
        } else {
            // We just need to wait for the last thread to come, and then we are done. 
            if (quantum_barrier.last_thread_entered) {
                // We need to wait for the last thread to finish its job.
                while(quantum_barrier.last_thread_entered) {
                    // do nothing
                }

                // Return, because the quantum is resolved.
                assert(quantum_barrier.quantum_resolution_result == QUANTUM_COMPLETE);
                result->quantum_resolution_result = QUANTUM_COMPLETE;
                result->generation = quantum_barrier.generation;
                return;
            }
        }
    }

    // It is impossible to reach here.
    assert(false);
};

void quantum_recharge(int64_t threshold, quantum_per_thread_data_t *per_thread_data) {
    
    quantum_barrier_resolution_result_t result;    
    while(per_thread_data->credit <= threshold) {
        quantum_barrier_wait(0, &result);
        // This must return a complete quantum. 
        assert(result.quantum_resolution_result == QUANTUM_COMPLETE);
        // Now, based on the result, we need to update the target time and the local credit.
        uint64_t local_target_time_in_10ps = (result.generation * quantum_size * 100) - (per_thread_data->credit * 10000 / (int64_t)per_thread_data->ip100ns);
        cpu_virtual_time[current_cpu->cpu_index].vts = local_target_time_in_10ps;
        // Now, we update the credit and the generation accordingly. 
        per_thread_data->credit += (quantum_size * per_thread_data->ip100ns) / 100;
        assert(result.generation == per_thread_data->generation + 1);
        per_thread_data->generation = result.generation;
    }
}

void quantum_suspend(quantum_per_thread_data_t *per_thread_data) {
    assert(per_thread_data == &current_cpu->quantum_data);
    // Before calling the wait function, we need to process a few things in the local thread.
    // 1. Calculate the remaining credit, in the unit of time. 
    per_thread_data->credit_in_10ps = per_thread_data->credit * 10000 / per_thread_data->ip100ns;

    quantum_barrier_resolution_result_t result;

    quantum_barrier_wait(1, &result);
    switch (result.quantum_resolution_result) {
        case QUANTUM_CONTINUE: {
            // The thread should continue. 
            // If the thread is waken by IPI, another thread should already put the credit update request in the queue.
            return;
        }
        case QUANTUM_COMPLETE: {
            // Then, the local time should be updated properly. It is now only aligned with the quantum barrier.
            int64_t local_target_time_in_10ps = result.generation * quantum_size * 100;
            cpu_virtual_time[current_cpu->cpu_index].vts = local_target_time_in_10ps;
            // The credit is aligned with the quantum barrier.
            per_thread_data->credit = (quantum_size * per_thread_data->ip100ns) / 100;
            // the generation is updated accordingly. 
            per_thread_data->generation = result.generation;
            break;
        }
        case QUANTUM_INCOMPLETE: {
            // Well, this mode means the credit_in_time is already set by the quantum barrier. 
            // We need to recover the credit, the local time, and the generation from it.
            per_thread_data->generation = result.generation;
            // The current local target time is calculated by the credit_in_time, which records the amount of time this thread can move forward in this quantum.
            int64_t local_target_time_in_10ps = per_thread_data->generation * quantum_size * 100 - per_thread_data->credit_in_10ps; 
            cpu_virtual_time[current_cpu->cpu_index].vts = local_target_time_in_10ps;
            // The credit is calculated by the credit_in_time.
            per_thread_data->credit = per_thread_data->credit_in_10ps * per_thread_data->ip100ns / 10000;
            break;
        }
    }
}
