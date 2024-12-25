/*
 * QEMU TCG Multi Threaded vCPUs implementation
 *
 * Copyright (c) 2003-2008 Fabrice Bellard
 * Copyright (c) 2014 Red Hat Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL
 * THE AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 */

#include "qemu/osdep.h"
#include "sysemu/tcg.h"
#include "qemu/plugin-cyan.h"
#include "qemu/typedefs.h"
#include "sysemu/replay.h"
#include "sysemu/cpu-timers.h"
#include "qemu/main-loop.h"
#include "qemu/notify.h"
#include "qemu/guest-random.h"
#include "exec/exec-all.h"
#include "hw/boards.h"
#include "tcg/tcg.h"
#include "tcg-accel-ops.h"
#include "tcg-accel-ops-mttcg.h"
#include "qemu/log-for-trace.h"

#include "qemu/dynamic_barrier.h"
#include "sysemu/quantum.h"
#include <stdio.h>

// const uint64_t QUANTUM_SIZE = 1000000; // 1M
dynamic_barrier_polling_t quantum_barrier;

typedef struct core_meta_info_t {
    uint64_t ip10ps;
    uint64_t affinity_core_idx;
} core_meta_info_t;

static core_meta_info_t core_info_table[256];

void mttcg_initialize_core_info_table(const char *file_name) {
    // By default, all cores' IPC is 0, which means not managed by the IPC and the quantum.
    for(uint64_t i = 0; i < 256; ++i) {
        core_info_table[i].ip10ps = 0;
        core_info_table[i].affinity_core_idx = i;
    }

    // Load the IPC from the file. Each line is a integer and suggests the IPC.
    FILE *fp = fopen(file_name, "r");
    if (!fp) {
        if (quantum_enabled()) {
            // we don't do anything if the file is not found.
            printf("IPC file (%s) is not found. It is needed under the quantum mode\n", file_name);
            exit(1);
        } else {
            return;
        }
        
    }

    char line[1024];
    int core_id = 0;

    // The first line is the header.
    // The header is "ipc,affinity_core_idx"
    // do a comparison with the header.
    assert(fgets(line, 1024, fp) != NULL);
    assert(strcmp(line, "ip10ps,affinity_core_idx\n") == 0);    


    // Now, read every line and fill the structure.
    while(fgets(line, 1024, fp) != NULL) {
        char *token = strtok(line, ",");
        double ipc = strtod(token, NULL);
        core_info_table[core_id].ip10ps = (uint64_t)(ipc * 100);
        assert(core_info_table[core_id].ip10ps > 0 && "IPC should be greater than 0");
        token = strtok(NULL, ",");
        core_info_table[core_id].affinity_core_idx = atoi(token);
        core_id += 1;
    }

    fclose(fp);
}


typedef struct MttcgForceRcuNotifier {
    Notifier notifier;
    CPUState *cpu;
} MttcgForceRcuNotifier;

static void do_nothing(CPUState *cpu, run_on_cpu_data d)
{
}

static void mttcg_force_rcu(Notifier *notify, void *data)
{
    CPUState *cpu = container_of(notify, MttcgForceRcuNotifier, notifier)->cpu;

    /*
     * Called with rcu_registry_lock held, using async_run_on_cpu() ensures
     * that there are no deadlocks.
     */
    async_run_on_cpu(cpu, do_nothing, RUN_ON_CPU_NULL);
}

static uint64_t get_current_timestamp_ns(void) {
    struct timespec ts;
    // Get the current time
    clock_gettime(CLOCK_REALTIME, &ts);

    // Convert to nanoseconds
    // tv_sec is seconds, tv_nsec is nanoseconds
    uint64_t timestamp_ns = (uint64_t)ts.tv_sec * 1000000000LL + ts.tv_nsec;

    return timestamp_ns;
}

typedef struct {
    uint64_t total_time;
    uint64_t execution_time;
    uint64_t waiting_time;
    uint64_t idle_time;
    uint64_t peeking_other_time;
} per_cpu_host_time_breakdown_t;

const uint64_t RECORD_SIZE = 4096;
const uint64_t SKIP_SIZE = 1000;

static void dump_log(CPUState *cpu, per_cpu_host_time_breakdown_t *statistics) {
    // open a log file.
    char log_name[100];
    snprintf(log_name, 100, "qlog/statisitcs_%u.log", cpu->cpu_index);

    FILE *fp = fopen(log_name, "w");

    // push the enter_idle_time, target_cycle_on_idle, and target_cycle_on_instruction to the statistics.
    fprintf(
        fp, 
        "%lu,%lu,%lu\n", 
        cpu->enter_idle_time, 
        cpu->target_cycle_on_idle, 
        cpu->target_cycle_on_instruction
    );

    for(uint64_t i = 0; i < RECORD_SIZE; ++i) {
        fprintf(
            fp, 
            "%lu,%lu,%lu,%lu,%lu\n", 
            statistics[i].total_time, 
            statistics[i].execution_time, 
            statistics[i].waiting_time, 
            statistics[i].idle_time, 
            statistics[i].peeking_other_time
        );
    }

    fclose(fp);
}

/*
 * In the multi-threaded case each vCPU has its own thread. The TLS
 * variable current_cpu can be used deep in the code to find the
 * current CPUState for a given thread.
 */

static void *mttcg_cpu_thread_fn(void *arg)
{
    MttcgForceRcuNotifier force_rcu;
    CPUState *cpu = arg;

    cpu->ip10ps = core_info_table[cpu->cpu_index].ip10ps;

    assert(tcg_enabled());
    g_assert(!icount_enabled());

    rcu_register_thread();
    force_rcu.notifier.notify = mttcg_force_rcu;
    force_rcu.cpu = cpu;
    rcu_add_force_rcu_notifier(&force_rcu.notifier);
    tcg_register_thread();

    qemu_mutex_lock_iothread();
    qemu_thread_get_self(cpu->thread);

    // per_cpu_host_time_breakdown_t statistics[RECORD_SIZE];
    // bzero(statistics, sizeof(per_cpu_host_time_breakdown_t) * RECORD_SIZE);
    // uint64_t statistic_head_counter = 0;

    cpu->thread_id = qemu_get_thread_id();
    cpu->can_do_io = 1;


    current_cpu = cpu;
    cpu_thread_signal_created(cpu);
    qemu_guest_random_seed_thread_part2(cpu->random_seed);

    assert(cpu->cpu_index < 128);

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_info_table[cpu->cpu_index].affinity_core_idx, &cpuset);
    pthread_setaffinity_np(pthread_self(), sizeof(cpu_set_t), &cpuset);

    /* process any pending work */
    cpu->exit_request = 1;

    bool not_running_yet = true;

    bool affiliated_with_quantum = cpu->ip10ps && quantum_enabled();

    // uint64_t dumping_threshold = 300 * 1000 * 1000; // 300M

    // uint64_t ts0 = get_current_timestamp_ns();
    do {
        if (cpu_can_run(cpu)) {
            if (not_running_yet) {
                // initialize the field that are related to the time. 
                cpu->unknown_time = 0;
                cpu->enter_idle_time = 0;
                cpu->target_cycle_on_idle = 0;
                cpu->target_cycle_on_instruction = 0;

                // register the current thread to the barrier.
                if (affiliated_with_quantum) {
                    cpu->quantum_generation = dynamic_barrier_polling_increase_by_1(&quantum_barrier);
                    cpu->quantum_budget = (quantum_size * cpu->ip10ps) / 100;
                    assert(cpu->quantum_budget > 0); 
                    cpu->quantum_required = 0;
                    cpu->quantum_budget_depleted = 0;


                    qemu_log("Core%u Quantum Count: %lu cycles, %lu instructions \n", cpu->cpu_index, quantum_size, quantum_size * cpu->ip10ps);
                }

                // initialize the quantum budget.
                not_running_yet = false;
            }

            int r;
            qemu_mutex_unlock_iothread();
            r = tcg_cpus_exec(cpu);
            // check the quantum budget and sync before doing I/O operation.
            if (cpu->quantum_budget_depleted) {
                cpu->quantum_budget_depleted = false;
                if (affiliated_with_quantum) {
                    while (cpu->quantum_budget <= 0) {
                        uint64_t old_generation = cpu->quantum_generation;
                        cpu_virtual_time[cpu->cpu_index].next_deadline_in_ns = -1;
                        bool stop_request = false;

                        uint64_t new_generation = dynamic_barrier_polling_wait(&quantum_barrier, cpu->quantum_generation, &stop_request);
            
                        
                        assert(new_generation == old_generation + 1);
                        cpu->quantum_budget += (quantum_size * cpu->ip10ps) / 100;
                        cpu->quantum_generation = new_generation;

                        if (stop_request) {
                            break;
                        }
                    }
                    
                    if (r == EXCP_QUANTUM) {
                        // We don't do anything here. We will go to the idle state and conduct a wait, because it is possible that a checkpoint is required to be taken.
                    }
                } else {
                    assert(false);
                }
            }
            qemu_mutex_lock_iothread();
            switch (r) {            
            case EXCP_DEBUG:
                cpu_handle_guest_debug(cpu);
                break;
            case EXCP_HALTED:
                /*
                 * Usually cpu->halted is set, but may have already been
                 * reset by another thread by the time we arrive here.
                 */
                break;
            case EXCP_ATOMIC:
                qemu_mutex_unlock_iothread();
                // Well, it is possible that this atomic step may deplete the quantum budget.
                // What we have to do now is to give enough quantum budget to this CPU, and remove it afterwards. 
                int64_t quantum_for_deduction = cpu->quantum_required;
                // We need to sync immediately to get the quantum budget. 
                if (affiliated_with_quantum) {
                    while (cpu->quantum_budget <= quantum_for_deduction) {
                        uint64_t old_generation = cpu->quantum_generation;
                        cpu_virtual_time[cpu->cpu_index].next_deadline_in_ns = -1;
                        bool stop_request = false;

                        uint64_t new_generation = dynamic_barrier_polling_wait(&quantum_barrier, cpu->quantum_generation, &stop_request);
            
                        
                        assert(new_generation == old_generation + 1);
                        cpu->quantum_budget += (quantum_size * cpu->ip10ps) / 100;
                        cpu->quantum_generation = new_generation;

                        if (stop_request) {
                            break;
                        }
                    }
                }
                assert(cpu->quantum_budget_depleted == false);
                cpu_exec_step_atomic(cpu);
                qemu_mutex_lock_iothread();
            default:
                /* Ignore everything else? */
                break;
            }
        }

        qatomic_set_mb(&cpu->exit_request, 0);
        uint32_t current_quantum_generation = 0;
        qemu_wait_io_event(cpu, not_running_yet, &current_quantum_generation); // This function will not decouple the thread from the barrier anymore.

        // it is possible that the quantum budget is depleted due to the idle state.
        if (affiliated_with_quantum && cpu->quantum_budget_depleted) {
            qemu_mutex_unlock_iothread();
            cpu->quantum_budget_depleted = false;
            if (affiliated_with_quantum) {
                while (cpu->quantum_budget <= 0) {
                    uint64_t old_generation = cpu->quantum_generation;
                    cpu_virtual_time[cpu->cpu_index].next_deadline_in_ns = -1;
                    bool stop_request = false;

                    uint64_t new_generation = dynamic_barrier_polling_wait(&quantum_barrier, cpu->quantum_generation, &stop_request);
        
                    assert(new_generation == old_generation + 1);
                    cpu->quantum_budget += (quantum_size * cpu->ip10ps) / 100;
                    cpu->quantum_generation = new_generation;

                    if (stop_request) {
                        break;
                    }
                }
            } else {
                assert(false);
            }
            qemu_mutex_lock_iothread();
        }
    } while (!cpu->unplug || cpu_can_run(cpu));

    tcg_cpus_destroy(cpu);
    qemu_mutex_unlock_iothread();
    rcu_remove_force_rcu_notifier(&force_rcu.notifier);
    rcu_unregister_thread();

    // resign the current thread from the barrier.
    if (affiliated_with_quantum) {
        dynamic_barrier_polling_decrease_by_1(&quantum_barrier);

        // also, print the histogram.
        // open a log file.
        char log_name[100];
        snprintf(log_name, 100, "quantum_histogram_%d.log", cpu->cpu_index);

        FILE *fp = fopen(log_name, "w");
        print_histogram(quantum_barrier.histogram[cpu->cpu_index], fp);
        fclose(fp);
    }

    return NULL;
}

void mttcg_kick_vcpu_thread(CPUState *cpu)
{
    cpu_exit(cpu);
}

void mttcg_start_vcpu_thread(CPUState *cpu)
{
    char thread_name[VCPU_THREAD_NAME_SIZE];

    g_assert(tcg_enabled());
    tcg_cpu_init_cflags(cpu, current_machine->smp.max_cpus > 1);

    cpu->thread = g_new0(QemuThread, 1);
    cpu->halt_cond = g_malloc0(sizeof(QemuCond));
    qemu_cond_init(cpu->halt_cond);

    /* create a thread per vCPU with TCG (MTTCG) */
    snprintf(thread_name, VCPU_THREAD_NAME_SIZE, "CPU %d/TCG",
             cpu->cpu_index);

    qemu_thread_create(cpu->thread, thread_name, mttcg_cpu_thread_fn,
                       cpu, QEMU_THREAD_JOINABLE);
}

void mttcg_initialize_barrier(void) {
    dynamic_barrier_polling_init(&quantum_barrier, 0);
}
