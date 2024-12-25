// This file is added by Cyan for the quantum mechanism.

#include "qemu/osdep.h"
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

    // deduction.
    current_cpu->quantum_budget -= current_cpu->quantum_required;

    // increase the target cycle.
    uint64_t current_index = current_cpu->cpu_index;
    cpu_virtual_time[current_index].vts += current_cpu->quantum_required * 10000 / current_cpu->ip10ps;

    current_cpu->quantum_required = 0;

}

uint32_t HELPER(check_and_deduce_quantum)(CPUArchState *env) {
    assert(quantum_enabled());
    assert(current_cpu->env_ptr == env);

    if (current_cpu->ip10ps == 0) {
        return false;
    }

    current_cpu->target_cycle_on_instruction += current_cpu->quantum_required;

    // deduction.
    current_cpu->quantum_budget -= current_cpu->quantum_required;

    // increase the target cycle.
    uint64_t current_index = current_cpu->cpu_index;
    cpu_virtual_time[current_index].vts += current_cpu->quantum_required * 10000 / current_cpu->ip10ps;

    current_cpu->quantum_required = 0;
    
    if (current_cpu->quantum_budget <= 0) {
        current_cpu->quantum_budget_depleted = 1;
        return true;
    }
    return false;
}

void HELPER(set_quantum_requirement_example)(CPUArchState *env, uint32_t requirement) {
    assert(quantum_enabled() || icount_enabled());
    current_cpu->quantum_required = requirement;
}

void HELPER(increase_target_cycle)(CPUArchState *env) {
    assert(icount_enabled());

    uint64_t current_index = current_cpu->cpu_index;
    cpu_virtual_time[current_index].vts += current_cpu->quantum_required * 10000 / current_cpu->ip10ps;

    current_cpu->quantum_required = 0;
}