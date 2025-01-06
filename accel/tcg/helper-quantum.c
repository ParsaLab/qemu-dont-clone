// This file is added by Cyan for the quantum mechanism.

#include "qemu/osdep.h"
#include "qemu/quantum.h"
#include "qemu/typedefs.h"
#include "qemu/units.h"
#include "gdbstub/helpers.h"
#include "exec/helper-proto.h"
#include "cpu.h"
#include "hw/core/cpu.h"
#include "sysemu/cpu-timers.h"
#include "sysemu/quantum.h"
#include "qemu/plugin-cyan.h"

void HELPER(deduce_quantum)(CPUArchState *env) {
    assert(quantum_enabled());

    assert(current_cpu->env_ptr == env);
    quantum_per_thread_data_t *per_thread_data = &current_cpu->quantum_data;

    // deduction.
    per_thread_data->credit -= per_thread_data->credit_required;

    // increase the target cycle.
    uint64_t current_index = current_cpu->cpu_index;
    cpu_virtual_time[current_index].vts += per_thread_data->credit_required * 10000 / per_thread_data->ip100ns;

    per_thread_data->credit_required = 0;
}

uint32_t HELPER(check_and_deduce_quantum)(CPUArchState *env) {
    assert(quantum_enabled());
    assert(current_cpu->env_ptr == env);
    quantum_per_thread_data_t *per_thread_data = &current_cpu->quantum_data;


    if (per_thread_data->ip100ns == 0) {
        return false;
    }

    // current_cpu->target_cycle_on_instruction += current_cpu->quantum_required;

    // deduction.
    per_thread_data->credit -= per_thread_data->credit_required;

    // increase the target cycle.
    uint64_t current_index = current_cpu->cpu_index;
    cpu_virtual_time[current_index].vts += per_thread_data->credit_required * 10000 / per_thread_data->ip100ns;

    per_thread_data->credit_required = 0;
    
    if (per_thread_data->credit <= 0) {
        per_thread_data->credit_depleted = 1;
        return true;
    }
    return false;
}

void HELPER(set_quantum_requirement_example)(CPUArchState *env, uint32_t requirement) {
    assert(quantum_enabled() || icount_enabled());
    current_cpu->quantum_data.credit_required = requirement;
}

void HELPER(increase_target_cycle)(CPUArchState *env) {
    assert(icount_enabled());
    quantum_per_thread_data_t *per_thread_data = &current_cpu->quantum_data;

    uint64_t current_index = current_cpu->cpu_index;
    cpu_virtual_time[current_index].vts += per_thread_data->credit_required * 10000 / per_thread_data->ip100ns;

    current_cpu->quantum_data.credit_required = 0;
}