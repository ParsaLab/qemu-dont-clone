
#include "qemu/osdep.h"
#include "qemu/quantum.h"
#include "sysemu/quantum.h"
#include "hw/core/cpu.h"
#include "qemu/timer.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/cpus.h"

quantum_barrier_t quantum_barrier;

void quantum_barrier_init(quantum_barrier_t *barrier) {
    atomic_store(&barrier->lock, 0);
    
    barrier->suspended_thread_count = 0;
    barrier->synchronizing_thread_count = 0;
    
    atomic_store(&barrier->last_thread_entered, 0);
    atomic_store(&barrier->quantum_resolution_result, 0);
    
    barrier->threshold = 0;
    barrier->generation = 0;
    barrier->passed_time = 0;

    for (int i = 0; i < 256; i++) {
        barrier->per_thread_data[i] = NULL;
    }

    barrier->next_check_threshold = 0;
}

void quantum_barrier_lock(quantum_barrier_t *barrier) {
    while (atomic_exchange(&barrier->lock, 1) == 1) {
        // do nothing
    }
}

void quantum_barrier_unlock(quantum_barrier_t *barrier) {
    atomic_store(&barrier->lock, 0);
}

void quantum_barrier_register(quantum_barrier_t *barrier, uint64_t cpu_index, quamtum_per_thread_data_t *per_thread_data) {
    quantum_barrier_lock(barrier);

    assert(barrier->threshold < 256);
    barrier->per_thread_data[cpu_index] = per_thread_data;
    barrier->threshold += 1;

    quantum_barrier_unlock(barrier);
}

void quantum_barrier_wait(struct quantum_barrier_t *barrier, uint64_t is_suspended, quantum_barrier_resolution_result_t *result) {
    // First of all, get the lock of the quantum barrier. 
    quantum_barrier_lock(barrier);

    // Now, if the thread is suspended, increase the suspended_thread_count.
    if (is_suspended) {
        barrier->suspended_thread_count += 1;
    } else {
        barrier->synchronizing_thread_count += 1;
    }

    // Check whether the current quantum is a complete one.
    if (barrier->suspended_thread_count + barrier->synchronizing_thread_count == barrier->threshold) {
        // This is the last thread.
        barrier->last_thread_entered = 1; // cut off. Now all threads should be in the synchronization mode and cannot leave from the quantum barrier. 

        // Now, all threads are in the synchronizing mode. We need to calculate the target time based on all threads' local target time.

        // Now, we need to decide what to do. 
        uint64_t is_complete = barrier->synchronizing_thread_count > 0;
        if (is_complete) {
            // well, we don't have much thing to do. All threads clean their credit and go to the next quantum. 
            barrier->quantum_resolution_result = 1;
            
            // update the barrier generation.
            barrier->generation += 1;
            // update the passed time.
            barrier->passed_time += quantum_size;

            barrier->last_thread_entered = 0; // this action will activate all synchronizing threads.
            
            // release the lock.
            atomic_store(&barrier->lock, 0);

            // return. 
            result->quantum_resolution_result = QUANTUM_COMPLETE;
            result->generation = barrier->generation; 
            return;
        } else {
            barrier->quantum_resolution_result = 0; // This is an incomplete quantum.

            // Now, we have a lot of trouble. All threads are in the suspended mode. 
            // 1. Suspend the advancement of the time. In this case, we can safely stop all threads without handling the timer interrupt outdated. 
            cpu_disable_ticks();

            // 2. Find the latest thread and know its remaining credit (in the unit of time.)
            uint64_t least_credit_in_time_10ps = 0xFFFFFFFFFFFFFFFF;
            for (int i = 0; i < barrier->threshold; i++) {
                if (barrier->per_thread_data[i]->credit_in_time < least_credit_in_time_10ps) {
                    least_credit_in_time_10ps = barrier->per_thread_data[i]->credit_in_time;
                }
            }

            // 3. Find the most recent deadline and calculate the amount of time that we should fast forward.
            uint64_t most_recent_deadline_in_virtual_time = 0xFFFFFFFFFFFFFFFF;
            for (int i = 0; i < barrier->threshold; i++) {
                if (barrier->per_thread_data[i]->most_recent_deadline_in_virtual_time < most_recent_deadline_in_virtual_time) {
                    most_recent_deadline_in_virtual_time = barrier->per_thread_data[i]->most_recent_deadline_in_virtual_time;
                }
            }

            uint64_t current_vtime = qemu_clock_get_ns(QEMU_CLOCK_VIRTUAL);

            assert(current_vtime != -1);

            int64_t time_to_fast_forward = most_recent_deadline_in_virtual_time - current_vtime;

            assert(time_to_fast_forward >= 0);

            // Run the fast forwarding.
            cpu_shift_vm_clock(time_to_fast_forward);

            int64_t remaining_credit_in_time = least_credit_in_time_10ps - time_to_fast_forward * 100;

            if (remaining_credit_in_time < 0) {
                // Well, the current quantum is deplete after fast forwarding. We need to go to the next quantum.
                while (remaining_credit_in_time < 0) {
                    remaining_credit_in_time += quantum_size * 100;
                    barrier->generation += 1;
                    barrier->passed_time += quantum_size;
                }
            } else {
                // The current quantum still has some credit after fast forwarding. We need to adjust the credit of each thread.
                barrier->passed_time += (quantum_size - remaining_credit_in_time / 100);
            }

            // 4. Recalculate the credit of each thread.
            for (int i = 0; i < barrier->threshold; i++) {
                barrier->per_thread_data[i]->credit_in_time = remaining_credit_in_time;
                barrier->per_thread_data[i]->credit = barrier->per_thread_data[i]->credit_in_time * barrier->per_thread_data[i]->ip10ps;
            }

            // 5. Resume the advancement of the time. 
            cpu_enable_ticks();

            barrier->last_thread_entered = 0; // this action will activate all synchronizing threads.

            // 6. release the lock.
            atomic_store(&barrier->lock, 0);

            // No need to fill the quantum credit by each thread anymore. 
            result->quantum_resolution_result = QUANTUM_INCOMPLETE;
            result->generation = barrier->generation;

            return;
        }
    } else {
        // This is not the last thread. Relax the lock.
        atomic_store(&barrier->lock, 0);

        // Depending on the is_suspended, wait. 
        if (is_suspended) {
            while (1) {
                if (cpu_can_run(current_cpu)) {
                    // The thread can run. It should go to another state. 
                    // Before doing that, we need to decrease the suspended_thread_count.
                    // Get the lock`=-0
                    while (atomic_exchange(&barrier->lock, 1) == 1) {
                        // do nothing
                    }

                    // Decrease the suspended_thread_count.
                    barrier->suspended_thread_count -= 1;

                    // Release the lock.
                    atomic_store(&barrier->lock, 0);
                    result->quantum_resolution_result = QUANTUM_CONTINUE;

                    return;
                }

                // If the last thread has appeared during the waiting process, we need to go to the synchronizing mode.
                if (barrier->last_thread_entered) {
                    // We need to wait for the last thread to finish its job.
                    while(barrier->last_thread_entered) {
                        // do nothing
                    }
                    
                    if (barrier->quantum_resolution_result) {
                        // The quantum is complete.
                        result->quantum_resolution_result = QUANTUM_COMPLETE;
                        result->generation = barrier->generation;

                        return;
                    } else {
                        result->quantum_resolution_result = QUANTUM_INCOMPLETE; // in this case, the return value is stored in the credit_in_time.
                        result->generation = barrier->generation;

                        return;
                    }
                }
            }
        } else {
            // We just need to wait for the last thread to come, and then we are done. 
            if (barrier->last_thread_entered) {
                // We need to wait for the last thread to finish its job.
                while(barrier->last_thread_entered) {
                    // do nothing
                }
                
                if (barrier->quantum_resolution_result) {
                    // The quantum is complete.
                    result->quantum_resolution_result = QUANTUM_COMPLETE;
                    result->generation = barrier->generation;
                    return;

                } else {
                    assert(false); // Impossible. After all, this thread comes from the synchronizing mode.
                }
            }
        }
    }

    // It is impossible to reach here.
    assert(false);
};



