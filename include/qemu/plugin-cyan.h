/**
 * @file include/qemu/plugin-cyan.h
 * 
 * This file contains the definitions for the Cyan's plugin system.
 */

#ifndef QEMU_PLUGIN_CYAN_H
#define QEMU_PLUGIN_CYAN_H

#include "qemu/qemu-plugin.h"

// This file keeps the following callbacks for the plugin system:
// - Callback for virtual time calculation.
// - Callback for branch resolution.
// - Callback for savevm (after the VM state is saved).

// The callback for virtual time calculation.
extern qemu_plugin_cpu_clock_callback_t cyan_cpu_clock_cb;

// The callback for branch resolution.
extern qemu_plugin_vcpu_branch_resolved_cb_t cyan_br_cb;

// The callback for savevm (after the VM state is saved).
extern qemu_plugin_snapshot_cb_t cyan_savevm_cb;

// The callback for loadvm (after the VM state is loaded).
extern qemu_plugin_snapshot_cb_t cyan_loadvm_cb;

// The callback for the moment when the snapshot CPU clock is updated.
extern qemu_plugin_snapshot_cpu_clock_update_cb cyan_snapshot_cpu_clock_udpate_cb;

// The event loop polling callback for the plugin system.
extern qemu_plugin_event_loop_poll_cb_t cyan_el_pool_cb;

// The periodic check callback for the plugin system.
extern qemu_plugin_periodic_check_cb_t cyan_periodic_check_cb;

extern qemu_plugin_flushing_local_tlb_t cyan_flushing_local_tlb_cb;

struct cpu_virtual_time_t {
  uint64_t vts;
  uint64_t next_deadline_in_ns;
  uint64_t __padding[6];
};

extern struct cpu_virtual_time_t cpu_virtual_time[256];

#endif
