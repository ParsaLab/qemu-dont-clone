/*
 * Copyright (C) 2017, Emilio G. Cota <cota@braap.org>
 * Copyright (C) 2019, Linaro
 *
 * License: GNU GPL, version 2 or later.
 *   See the COPYING file in the top-level directory.
 *
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

#ifndef QEMU_QEMU_PLUGIN_H
#define QEMU_QEMU_PLUGIN_H

#include <inttypes.h>
#include <stdbool.h>
#include <stddef.h>

/*
 * For best performance, build the plugin with -fvisibility=hidden so that
 * QEMU_PLUGIN_LOCAL is implicit. Then, just mark qemu_plugin_install with
 * QEMU_PLUGIN_EXPORT. For more info, see
 *   https://gcc.gnu.org/wiki/Visibility
 */
#if defined _WIN32 || defined __CYGWIN__
#ifdef BUILDING_DLL
#define QEMU_PLUGIN_EXPORT __declspec(dllexport)
#else
#define QEMU_PLUGIN_EXPORT __declspec(dllimport)
#endif
#define QEMU_PLUGIN_LOCAL
#else
#define QEMU_PLUGIN_EXPORT __attribute__((visibility("default")))
#define QEMU_PLUGIN_LOCAL __attribute__((visibility("hidden")))
#endif

/**
 * typedef qemu_plugin_id_t - Unique plugin ID
 */
typedef uint64_t qemu_plugin_id_t;

/*
 * Versioning plugins:
 *
 * The plugin API will pass a minimum and current API version that
 * QEMU currently supports. The minimum API will be incremented if an
 * API needs to be deprecated.
 *
 * The plugins export the API they were built against by exposing the
 * symbol qemu_plugin_version which can be checked.
 */

extern QEMU_PLUGIN_EXPORT int qemu_plugin_version;

#define QEMU_PLUGIN_VERSION 1

#define QEMU_PLUGIN_CYAN_VERSION 9527

/**
 * struct qemu_info_t - system information for plugins
 *
 * This structure provides for some limited information about the
 * system to allow the plugin to make decisions on how to proceed. For
 * example it might only be suitable for running on some guest
 * architectures or when under full system emulation.
 */
typedef struct qemu_info_t {
  /** @target_name: string describing architecture */
  const char *target_name;
  /** @version: minimum and current plugin API level */
  struct {
    int min;
    int cur;
  } version;
  /** @system_emulation: is this a full system emulation? */
  bool system_emulation;
  union {
    /** @system: information relevant to system emulation */
    struct {
      /** @system.smp_vcpus: initial number of vCPUs */
      int smp_vcpus;
      /** @system.max_vcpus: maximum possible number of vCPUs */
      int max_vcpus;
    } system;
  };
} qemu_info_t;

/**
 * qemu_plugin_install() - Install a plugin
 * @id: this plugin's opaque ID
 * @info: a block describing some details about the guest
 * @argc: number of arguments
 * @argv: array of arguments (@argc elements)
 *
 * All plugins must export this symbol which is called when the plugin
 * is first loaded. Calling qemu_plugin_uninstall() from this function
 * is a bug.
 *
 * Note: @info is only live during the call. Copy any information we
 * want to keep. @argv remains valid throughout the lifetime of the
 * loaded plugin.
 *
 * Return: 0 on successful loading, !0 for an error.
 */
QEMU_PLUGIN_EXPORT int qemu_plugin_install(qemu_plugin_id_t id,
                                           const qemu_info_t *info, int argc,
                                           char **argv);

/**
 * typedef qemu_plugin_simple_cb_t - simple callback
 * @id: the unique qemu_plugin_id_t
 *
 * This callback passes no information aside from the unique @id.
 */
typedef void (*qemu_plugin_simple_cb_t)(qemu_plugin_id_t id);

/**
 * typedef qemu_plugin_udata_cb_t - callback with user data
 * @id: the unique qemu_plugin_id_t
 * @userdata: a pointer to some user data supplied when the callback
 * was registered.
 */
typedef void (*qemu_plugin_udata_cb_t)(qemu_plugin_id_t id, void *userdata);

/**
 * typedef qemu_plugin_vcpu_simple_cb_t - vcpu callback
 * @id: the unique qemu_plugin_id_t
 * @vcpu_index: the current vcpu context
 */
typedef void (*qemu_plugin_vcpu_simple_cb_t)(qemu_plugin_id_t id,
                                             unsigned int vcpu_index);

/**
 * typedef qemu_plugin_vcpu_udata_cb_t - vcpu callback
 * @vcpu_index: the current vcpu context
 * @userdata: a pointer to some user data supplied when the callback
 * was registered.
 */
typedef void (*qemu_plugin_vcpu_udata_cb_t)(unsigned int vcpu_index,
                                            void *userdata);

/**
 * qemu_plugin_uninstall() - Uninstall a plugin
 * @id: this plugin's opaque ID
 * @cb: callback to be called once the plugin has been removed
 *
 * Do NOT assume that the plugin has been uninstalled once this function
 * returns. Plugins are uninstalled asynchronously, and therefore the given
 * plugin receives callbacks until @cb is called.
 *
 * Note: Calling this function from qemu_plugin_install() is a bug.
 */
void qemu_plugin_uninstall(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb);

/**
 * qemu_plugin_reset() - Reset a plugin
 * @id: this plugin's opaque ID
 * @cb: callback to be called once the plugin has been reset
 *
 * Unregisters all callbacks for the plugin given by @id.
 *
 * Do NOT assume that the plugin has been reset once this function returns.
 * Plugins are reset asynchronously, and therefore the given plugin receives
 * callbacks until @cb is called.
 */
void qemu_plugin_reset(qemu_plugin_id_t id, qemu_plugin_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_init_cb() - register a vCPU initialization callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU is initialized.
 *
 * See also: qemu_plugin_register_vcpu_exit_cb()
 */
void qemu_plugin_register_vcpu_init_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_exit_cb() - register a vCPU exit callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU exits.
 *
 * See also: qemu_plugin_register_vcpu_init_cb()
 */
void qemu_plugin_register_vcpu_exit_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_idle_cb() - register a vCPU idle callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU idles.
 */
void qemu_plugin_register_vcpu_idle_cb(qemu_plugin_id_t id,
                                       qemu_plugin_vcpu_simple_cb_t cb);

/**
 * qemu_plugin_register_vcpu_resume_cb() - register a vCPU resume callback
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a vCPU resumes execution.
 */
void qemu_plugin_register_vcpu_resume_cb(qemu_plugin_id_t id,
                                         qemu_plugin_vcpu_simple_cb_t cb);

/** struct qemu_plugin_tb - Opaque handle for a translation block */
struct qemu_plugin_tb;
/** struct qemu_plugin_insn - Opaque handle for a translated instruction */
struct qemu_plugin_insn;

/**
 * enum qemu_plugin_cb_flags - type of callback
 *
 * @QEMU_PLUGIN_CB_NO_REGS: callback does not access the CPU's regs
 * @QEMU_PLUGIN_CB_R_REGS: callback reads the CPU's regs
 * @QEMU_PLUGIN_CB_RW_REGS: callback reads and writes the CPU's regs
 *
 * Note: currently unused, plugins cannot read or change system
 * register state.
 */
enum qemu_plugin_cb_flags {
  QEMU_PLUGIN_CB_NO_REGS,
  QEMU_PLUGIN_CB_R_REGS,
  QEMU_PLUGIN_CB_RW_REGS,
};

enum qemu_plugin_mem_rw {
  QEMU_PLUGIN_MEM_R = 1,
  QEMU_PLUGIN_MEM_W,
  QEMU_PLUGIN_MEM_RW,
};

/**
 * typedef qemu_plugin_vcpu_tb_trans_cb_t - translation callback
 * @id: unique plugin id
 * @tb: opaque handle used for querying and instrumenting a block.
 */
typedef void (*qemu_plugin_vcpu_tb_trans_cb_t)(qemu_plugin_id_t id,
                                               struct qemu_plugin_tb *tb);

/**
 * qemu_plugin_register_vcpu_tb_trans_cb() - register a translate cb
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called every time a translation occurs. The @cb
 * function is passed an opaque qemu_plugin_type which it can query
 * for additional information including the list of translated
 * instructions. At this point the plugin can register further
 * callbacks to be triggered when the block or individual instruction
 * executes.
 */
void qemu_plugin_register_vcpu_tb_trans_cb(qemu_plugin_id_t id,
                                           qemu_plugin_vcpu_tb_trans_cb_t cb);

/**
 * qemu_plugin_register_vcpu_tb_exec_cb() - register execution callback
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called every time a translated unit executes.
 */
void qemu_plugin_register_vcpu_tb_exec_cb(struct qemu_plugin_tb *tb,
                                          qemu_plugin_vcpu_udata_cb_t cb,
                                          enum qemu_plugin_cb_flags flags,
                                          void *userdata);

/**
 * enum qemu_plugin_op - describes an inline op
 *
 * @QEMU_PLUGIN_INLINE_ADD_U64: add an immediate value uint64_t
 *
 * Note: currently only a single inline op is supported.
 */

enum qemu_plugin_op {
  QEMU_PLUGIN_INLINE_ADD_U64,
};

/**
 * qemu_plugin_register_vcpu_tb_exec_inline() - execution inline op
 * @tb: the opaque qemu_plugin_tb handle for the translation
 * @op: the type of qemu_plugin_op (e.g. ADD_U64)
 * @ptr: the target memory location for the op
 * @imm: the op data (e.g. 1)
 *
 * Insert an inline op to every time a translated unit executes.
 * Useful if you just want to increment a single counter somewhere in
 * memory.
 *
 * Note: ops are not atomic so in multi-threaded/multi-smp situations
 * you will get inexact results.
 */
void qemu_plugin_register_vcpu_tb_exec_inline(struct qemu_plugin_tb *tb,
                                              enum qemu_plugin_op op, void *ptr,
                                              uint64_t imm);

/**
 * qemu_plugin_register_vcpu_insn_exec_cb() - register insn execution cb
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @cb: callback function
 * @flags: does the plugin read or write the CPU's registers?
 * @userdata: any plugin data to pass to the @cb?
 *
 * The @cb function is called every time an instruction is executed
 */
void qemu_plugin_register_vcpu_insn_exec_cb(struct qemu_plugin_insn *insn,
                                            qemu_plugin_vcpu_udata_cb_t cb,
                                            enum qemu_plugin_cb_flags flags,
                                            void *userdata);

/**
 * qemu_plugin_register_vcpu_insn_exec_inline() - insn execution inline op
 * @insn: the opaque qemu_plugin_insn handle for an instruction
 * @op: the type of qemu_plugin_op (e.g. ADD_U64)
 * @ptr: the target memory location for the op
 * @imm: the op data (e.g. 1)
 *
 * Insert an inline op to every time an instruction executes. Useful
 * if you just want to increment a single counter somewhere in memory.
 */
void qemu_plugin_register_vcpu_insn_exec_inline(struct qemu_plugin_insn *insn,
                                                enum qemu_plugin_op op,
                                                void *ptr, uint64_t imm);

/**
 * qemu_plugin_tb_n_insns() - query helper for number of insns in TB
 * @tb: opaque handle to TB passed to callback
 *
 * Returns: number of instructions in this block
 */
size_t qemu_plugin_tb_n_insns(const struct qemu_plugin_tb *tb);

/**
 * qemu_plugin_tb_vaddr() - query helper for vaddr of TB start
 * @tb: opaque handle to TB passed to callback
 *
 * Returns: virtual address of block start
 */
uint64_t qemu_plugin_tb_vaddr(const struct qemu_plugin_tb *tb);

/**
 * qemu_plugin_tb_get_insn() - retrieve handle for instruction
 * @tb: opaque handle to TB passed to callback
 * @idx: instruction number, 0 indexed
 *
 * The returned handle can be used in follow up helper queries as well
 * as when instrumenting an instruction. It is only valid for the
 * lifetime of the callback.
 *
 * Returns: opaque handle to instruction
 */
struct qemu_plugin_insn *
qemu_plugin_tb_get_insn(const struct qemu_plugin_tb *tb, size_t idx);

/**
 * qemu_plugin_insn_data() - return ptr to instruction data
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Note: data is only valid for duration of callback. See
 * qemu_plugin_insn_size() to calculate size of stream.
 *
 * Returns: pointer to a stream of bytes containing the value of this
 * instructions opcode.
 */
const void *qemu_plugin_insn_data(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_size() - return size of instruction
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Returns: size of instruction in bytes
 */
size_t qemu_plugin_insn_size(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_vaddr() - return vaddr of instruction
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Returns: virtual address of instruction
 */
uint64_t qemu_plugin_insn_vaddr(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_haddr() - return hardware addr of instruction
 * @insn: opaque instruction handle from qemu_plugin_tb_get_insn()
 *
 * Returns: hardware (physical) target address of instruction
 */
void *qemu_plugin_insn_haddr(const struct qemu_plugin_insn *insn);

/**
 * typedef qemu_plugin_meminfo_t - opaque memory transaction handle
 *
 * This can be further queried using the qemu_plugin_mem_* query
 * functions.
 */
typedef uint32_t qemu_plugin_meminfo_t;
/** struct qemu_plugin_hwaddr - opaque hw address handle */
struct qemu_plugin_hwaddr;

/**
 * qemu_plugin_mem_size_shift() - get size of access
 * @info: opaque memory transaction handle
 *
 * Returns: size of access in ^2 (0=byte, 1=16bit, 2=32bit etc...)
 */
unsigned int qemu_plugin_mem_size_shift(qemu_plugin_meminfo_t info);
/**
 * qemu_plugin_mem_is_sign_extended() - was the access sign extended
 * @info: opaque memory transaction handle
 *
 * Returns: true if it was, otherwise false
 */
bool qemu_plugin_mem_is_sign_extended(qemu_plugin_meminfo_t info);
/**
 * qemu_plugin_mem_is_big_endian() - was the access big endian
 * @info: opaque memory transaction handle
 *
 * Returns: true if it was, otherwise false
 */
bool qemu_plugin_mem_is_big_endian(qemu_plugin_meminfo_t info);
/**
 * qemu_plugin_mem_is_store() - was the access a store
 * @info: opaque memory transaction handle
 *
 * Returns: true if it was, otherwise false
 */
bool qemu_plugin_mem_is_store(qemu_plugin_meminfo_t info);

/**
 * qemu_plugin_get_hwaddr() - return handle for memory operation
 * @info: opaque memory info structure
 * @vaddr: the virtual address of the memory operation
 *
 * For system emulation returns a qemu_plugin_hwaddr handle to query
 * details about the actual physical address backing the virtual
 * address. For linux-user guests it just returns NULL.
 *
 * This handle is *only* valid for the duration of the callback. Any
 * information about the handle should be recovered before the
 * callback returns.
 */
struct qemu_plugin_hwaddr *qemu_plugin_get_hwaddr(qemu_plugin_meminfo_t info,
                                                  uint64_t vaddr);

/*
 * The following additional queries can be run on the hwaddr structure to
 * return information about it - namely whether it is for an IO access and the
 * physical address associated with the access.
 */

/**
 * qemu_plugin_hwaddr_is_io() - query whether memory operation is IO
 * @haddr: address handle from qemu_plugin_get_hwaddr()
 *
 * Returns true if the handle's memory operation is to memory-mapped IO, or
 * false if it is to RAM
 */
bool qemu_plugin_hwaddr_is_io(const struct qemu_plugin_hwaddr *haddr);

/**
 * qemu_plugin_hwaddr_phys_addr() - query physical address for memory operation
 * @haddr: address handle from qemu_plugin_get_hwaddr()
 *
 * Returns the physical address associated with the memory operation
 *
 * Note that the returned physical address may not be unique if you are dealing
 * with multiple address spaces.
 */
uint64_t qemu_plugin_hwaddr_phys_addr(const struct qemu_plugin_hwaddr *haddr);

/*
 * Returns a string representing the device. The string is valid for
 * the lifetime of the plugin.
 */
const char *qemu_plugin_hwaddr_device_name(const struct qemu_plugin_hwaddr *h);

/**
 * typedef qemu_plugin_vcpu_mem_cb_t - memory callback function type
 * @vcpu_index: the executing vCPU
 * @info: an opaque handle for further queries about the memory
 * @vaddr: the virtual address of the transaction
 * @userdata: any user data attached to the callback
 */
typedef void (*qemu_plugin_vcpu_mem_cb_t)(unsigned int vcpu_index,
                                          qemu_plugin_meminfo_t info,
                                          uint64_t vaddr, void *userdata);

/**
 * qemu_plugin_register_vcpu_mem_cb() - register memory access callback
 * @insn: handle for instruction to instrument
 * @cb: callback of type qemu_plugin_vcpu_mem_cb_t
 * @flags: (currently unused) callback flags
 * @rw: monitor reads, writes or both
 * @userdata: opaque pointer for userdata
 *
 * This registers a full callback for every memory access generated by
 * an instruction. If the instruction doesn't access memory no
 * callback will be made.
 *
 * The callback reports the vCPU the access took place on, the virtual
 * address of the access and a handle for further queries. The user
 * can attach some userdata to the callback for additional purposes.
 *
 * Other execution threads will continue to execute during the
 * callback so the plugin is responsible for ensuring it doesn't get
 * confused by making appropriate use of locking if required.
 */
void qemu_plugin_register_vcpu_mem_cb(struct qemu_plugin_insn *insn,
                                      qemu_plugin_vcpu_mem_cb_t cb,
                                      enum qemu_plugin_cb_flags flags,
                                      enum qemu_plugin_mem_rw rw,
                                      void *userdata);

/**
 * qemu_plugin_register_vcpu_mem_inline() - register an inline op to any memory
 * access
 * @insn: handle for instruction to instrument
 * @rw: apply to reads, writes or both
 * @op: the op, of type qemu_plugin_op
 * @ptr: pointer memory for the op
 * @imm: immediate data for @op
 *
 * This registers a inline op every memory access generated by the
 * instruction. This provides for a lightweight but not thread-safe
 * way of counting the number of operations done.
 */
void qemu_plugin_register_vcpu_mem_inline(struct qemu_plugin_insn *insn,
                                          enum qemu_plugin_mem_rw rw,
                                          enum qemu_plugin_op op, void *ptr,
                                          uint64_t imm);

typedef void (*qemu_plugin_vcpu_syscall_cb_t)(
    qemu_plugin_id_t id, unsigned int vcpu_index, int64_t num, uint64_t a1,
    uint64_t a2, uint64_t a3, uint64_t a4, uint64_t a5, uint64_t a6,
    uint64_t a7, uint64_t a8);

void qemu_plugin_register_vcpu_syscall_cb(qemu_plugin_id_t id,
                                          qemu_plugin_vcpu_syscall_cb_t cb);

typedef void (*qemu_plugin_vcpu_syscall_ret_cb_t)(qemu_plugin_id_t id,
                                                  unsigned int vcpu_idx,
                                                  int64_t num, int64_t ret);

void qemu_plugin_register_vcpu_syscall_ret_cb(
    qemu_plugin_id_t id, qemu_plugin_vcpu_syscall_ret_cb_t cb);

/**
 * qemu_plugin_insn_disas() - return disassembly string for instruction
 * @insn: instruction reference
 *
 * Returns an allocated string containing the disassembly
 */

char *qemu_plugin_insn_disas(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_insn_symbol() - best effort symbol lookup
 * @insn: instruction reference
 *
 * Return a static string referring to the symbol. This is dependent
 * on the binary QEMU is running having provided a symbol table.
 */
const char *qemu_plugin_insn_symbol(const struct qemu_plugin_insn *insn);

/**
 * qemu_plugin_vcpu_for_each() - iterate over the existing vCPU
 * @id: plugin ID
 * @cb: callback function
 *
 * The @cb function is called once for each existing vCPU.
 *
 * See also: qemu_plugin_register_vcpu_init_cb()
 */
void qemu_plugin_vcpu_for_each(qemu_plugin_id_t id,
                               qemu_plugin_vcpu_simple_cb_t cb);

void qemu_plugin_register_flush_cb(qemu_plugin_id_t id,
                                   qemu_plugin_simple_cb_t cb);

/**
 * qemu_plugin_register_atexit_cb() - register exit callback
 * @id: plugin ID
 * @cb: callback
 * @userdata: user data for callback
 *
 * The @cb function is called once execution has finished. Plugins
 * should be able to free all their resources at this point much like
 * after a reset/uninstall callback is called.
 *
 * In user-mode it is possible a few un-instrumented instructions from
 * child threads may run before the host kernel reaps the threads.
 */
void qemu_plugin_register_atexit_cb(qemu_plugin_id_t id,
                                    qemu_plugin_udata_cb_t cb, void *userdata);

/* returns -1 in user-mode */
int qemu_plugin_n_vcpus(void);

/* returns -1 in user-mode */
int qemu_plugin_n_max_vcpus(void);

/**
 * qemu_plugin_outs() - output string via QEMU's logging system
 * @string: a string
 */
void qemu_plugin_outs(const char *string);

/**
 * qemu_plugin_bool_parse() - parses a boolean argument in the form of
 * "<argname>=[on|yes|true|off|no|false]"
 *
 * @name: argument name, the part before the equals sign
 * @val: argument value, what's after the equals sign
 * @ret: output return value
 *
 * returns true if the combination @name=@val parses correctly to a boolean
 * argument, and false otherwise
 */
bool qemu_plugin_bool_parse(const char *name, const char *val, bool *ret);

/**
 * qemu_plugin_path_to_binary() - path to binary file being executed
 *
 * Return a string representing the path to the binary. For user-mode
 * this is the main executable. For system emulation we currently
 * return NULL. The user should g_free() the string once no longer
 * needed.
 */
const char *qemu_plugin_path_to_binary(void);

/**
 * qemu_plugin_start_code() - returns start of text segment
 *
 * Returns the nominal start address of the main text segment in
 * user-mode. Currently returns 0 for system emulation.
 */
uint64_t qemu_plugin_start_code(void);

/**
 * qemu_plugin_end_code() - returns end of text segment
 *
 * Returns the nominal end address of the main text segment in
 * user-mode. Currently returns 0 for system emulation.
 */
uint64_t qemu_plugin_end_code(void);

/**
 * qemu_plugin_entry_code() - returns start address for module
 *
 * Returns the nominal entry address of the main text segment in
 * user-mode. Currently returns 0 for system emulation.
 */
uint64_t qemu_plugin_entry_code(void);

#define CYAN_API
#define AARCH64_ONLY_API

/**
 * qemu_plugin_set_running_flag() - setting the "running" flag of the current
 * CPU
 *
 * @is_running: The value of the flag.
 *
 * Some synchronization mechanism (e.g., exclusive execution) checks
 * this flag to make sure all CPUs are not executing instructions and
 * wait for all CPUs to be idle.
 *
 * In you plugin are using locks and synchronization which can block
 * execution, you should set the running flag to false before being
 * blocked to avoid deadlocks.
 */
CYAN_API void qemu_plugin_set_running_flag(bool is_running);

/**
 * qemu_plugin_is_current_cpu_can_run() - check whether the current CPU can
 * still continue to run instructions, i.e., not stopped by other threads like
 * quitting.
 *
 * Returns true if the current cpu can still run.
 *
 * This function is a wrapper of function `cpu_can_run`.
 */
CYAN_API bool qemu_plugin_is_current_cpu_can_run(void);

CYAN_API typedef int64_t (*qemu_plugin_cpu_clock_callback_t)(void);

/**
 * qemu_plugin_register_cpu_clock_cb() - register the method for CPU to
 * calculate the time.
 *
 * @callback: The callback to provide cpu clock.
 *
 * Returns true if the registration is successful. Please note that only one
 * callback can be registered.
 *
 * This function overrides the internal QEMU function `cpu_get_clock_locked`,
 * and it cannot be used together with the icount mode.
 */
CYAN_API bool
qemu_plugin_register_cpu_clock_cb(qemu_plugin_cpu_clock_callback_t callback);

/**
 * qemu_plugin_get_cpu_clock() - return the CPU clock time calculated by the
 * realtime elapsing.
 *
 * Useful when defining the new cpu clock function.
 */
CYAN_API int64_t qemu_plugin_get_cpu_clock(void);

/**
 * qemu_plugin_get_snapshot_cpu_clock() - return the CPU clock when the snapshot
 * is taken. Otherwise, it is zero.
 *
 * Useful when defining the new cpu clock function.
 */
CYAN_API int64_t qemu_plugin_get_snapshot_cpu_clock(void);

CYAN_API typedef void (*qemu_plugin_snapshot_cpu_clock_update_cb)(void);

/**
 * qemu_plugin_register_snapshot_cpu_clock_update_cb() - register the callback
 * for updating the snapshot time.
 *
 * @callback: The callback to reset the VM clock.
 *
 * Returns true if the registration is successful. Please note that only one
 * callback can be registered.
 */

CYAN_API bool qemu_plugin_register_snapshot_cpu_clock_update_cb(
    qemu_plugin_snapshot_cpu_clock_update_cb callback);

/**
 * qemu_plugin_cpu_is_tick_enabled() - return whether the CPU tick is enabled.
 *
 * Useful when defining the new virtual time function.
 */

CYAN_API bool qemu_plugin_cpu_is_tick_enabled(void);

/**
 * qemu_plugin_read_cpu_integer_register - returns the value of the given
 * integer register.
 *
 * This function can be only called from threads that run a vCPU. Otherwise, it
 * will trigger assertion failure.
 */

CYAN_API AARCH64_ONLY_API uint64_t
qemu_plugin_read_cpu_integer_register(int reg_index);

/**
 * qemu_plugin_read_ttbr_el1 - returns the value of the ttbr_el1.
 *
 * @which_ttbr: 0 for ttbr0_el1, 1 for ttbr1_el1. Other values will trigger
 * assertion failure.
 *
 * This function can be only called from threads that run a vCPU. Otherwise, it
 * will trigger assertion failure.
 */
CYAN_API AARCH64_ONLY_API uint64_t qemu_plugin_read_ttbr_el1(int which_ttbr);

/**
 * qemu_plugin_read_tcr_el1 - returns the value of tcr_el1.
 *
 * This function can be only called from threads that run a vCPU. Otherwise, it
 * will trigger assertion failure.
 */

CYAN_API AARCH64_ONLY_API uint64_t qemu_plugin_read_tcr_el1(void);

/**
 * qemu_plugin_hwaddr_translate_walk_trace - returns the trace of walking the
 * page table to get the specific translation.
 *
 * The returned array has 4 elements. Every element is the hardware address of a
 * specific page table entry. For huge pages or translation error, you will see
 * -1 in the array ahead of time.
 *
 * This function can be only called from threads that run a vCPU. Otherwise, it
 * will return NULL.
 *
 * The function reads the recorded trace in the TLB entry. There is a better way
 * to optimize the storage.
 *
 */
CYAN_API AARCH64_ONLY_API const uint64_t *
qemu_plugin_hwaddr_translate_walk_trace(
    const struct qemu_plugin_hwaddr *hwaddr);

/**
 * qemu_plugin_read_physical_memory - returns the value of the given physical
 * memory address.
 *
 * This function calls cpu_physical_memory_rw to read the physical memory.
 *
 * This function will not trigger memory access plugin.
 */

CYAN_API void qemu_plugin_read_physical_memory(uint64_t physical_address,
                                               uint64_t size, void *buf);

/**
 * qemu_plugin_write_physical_memory - write the value to the given physical
 * memory address.
 *
 * This function calls the cpu_physical_memory_rw to write the physical memory.
 *
 * This function will not trigger memory access plugin.
 */

CYAN_API void qemu_plugin_write_physical_memory(uint64_t physical_address,
                                                uint64_t size, const void *buf);

/**
 * typedef qemu_plugin_vcpu_branch_resolved_cb_t - vcpu callback
 * @vcpu_index: the current vcpu context
 * @pc: the PC of the current instruction.
 * @target: the target address of the branch.
 * @hint_flags: the hint flags of the branch, including the following possible
 * values:
 *    - 0x0: conditional branch, taken
 *    - 0x1: conditional branch, not taken
 *    - 0x2: function call
 *    - 0x3: return
 *    - 0x4: non-conditional branch
 * was registered.
 */

CYAN_API AARCH64_ONLY_API typedef void (*qemu_plugin_vcpu_branch_resolved_cb_t)(
    unsigned int vcpu_index, uint64_t pc, uint64_t target, uint32_t hint_flags);

/**
 * qemu_plugin_register_vcpu_branch_resolved_cb() - register a vCPU branch
 * resolved callback
 * @cb: callback function
 *
 * The @cb function is called every time a branch is resolved.
 *
 * returns true if the callback is registered successfully. Please note at
 * currently at most one callback can be registered.
 *
 */
CYAN_API AARCH64_ONLY_API bool qemu_plugin_register_vcpu_branch_resolved_cb(
    qemu_plugin_vcpu_branch_resolved_cb_t cb);

/**
 * qemu_plugin_read_pc_vpn() - return the 4KB virtual page number of the current
 * PC.
 *
 * The reason why we return VPN is because QEMU does not frequent update the PC
 * in its CPUArchState. I still don't know why. It may be related to the
 * performance impact but I don't know where I should see the update logic.
 * However, the PC should be updated when the PC is pointing to a different
 * page. In this case, no chaining or patching of TB is allowed.
 *
 * This function can be only called from threads that run a vCPU. Otherwise, it
 * will trigger assertion failure.
 *
 * To get the full PC, you need to also store the PC's offset as the parameter
 * when registering the callback.
 *
 */
CYAN_API uint64_t qemu_plugin_read_pc_vpn(void);

CYAN_API typedef void (*qemu_plugin_snapshot_cb_t)(const char *);

/**
 * qemu_plugin_register_savevm_cb() - register a savevm callback
 * @cb: callback function
 *
 * The @cb function is called after the VM state is saved.
 * The exact time of the callback is after the VM state is saved to the qcow2
 * file and before the VM is resumed.
 *
 * returns true if the callback is registered successfully. Please note at
 * currently at most one callback can be registered.
 */
CYAN_API bool qemu_plugin_register_savevm_cb(qemu_plugin_snapshot_cb_t cb);

/**
 * qemu_plugin_register_loadvm_cb() - register a loadvm callback
 * @cb: callback function
 *
 * The @cb function is called after the VM state is loaded.
 *
 * returns true if the callback is registered successfully. Please note at
 * currently at most one callback can be registered.
 */
CYAN_API bool qemu_plugin_register_loadvm_cb(qemu_plugin_snapshot_cb_t cb);

/**
 * qemu_plugin_get_quantum_size - return the quantum size.
 *
 * Return 0 if the quantum is not enabled.
 */
CYAN_API uint64_t qemu_plugin_get_quantum_size(void);

/**
 * qemu_plugin_savevm - save the VM state.
 * @name: the name of the snapshot.
 *
 * This function is a wrapper of the QEMU function `save_snapshot`.
 * It prints the error directly to the console.
 */
CYAN_API void qemu_plugin_savevm(const char *name);

CYAN_API typedef void (*qemu_plugin_event_loop_poll_cb_t)(void);

/**
 * qemu_plugin_register_event_loop_poll_cb() - register a callback to poll the
 *
 * @cb: function is called every time the event loop polls.
 *
 * returns true if the callback is registered successfully.
 *
 * Please note at currently at most one callback can be registered.
 */
CYAN_API bool
qemu_plugin_register_event_loop_poll_cb(qemu_plugin_event_loop_poll_cb_t cb);

/**
 * qemu_plugin_is_icount_mode - return whether the icount mode is enabled.
 *
 * Returns true if the icount mode is enabled.
 */
CYAN_API bool qemu_plugin_is_icount_mode(void);

CYAN_API typedef bool (*qemu_plugin_periodic_check_cb_t)(
    uint64_t passed_cycles);

/**
 * qemu_plugin_register_periodic_check_cb() - register a callback for periodic
 * checking.
 *
 * @cb: function is called every time the periodic checking is triggered.
 *
 * When the function return trues, the VM stop request is sent, and all vCPUs
 * will be paused. This is useful for taking the snapshot of the system.
 *
 * There are two scenario when the periodic checking is triggered:
 * - When icount mode is on, and the `icount_checking_period` is set to
 * non-zero.
 * - When quantum mode is on, and the `quantum_checking_period` is set to
 * non-zero.
 *
 * Without these options, the periodic checking will not be triggered.
 *
 * The periodic checking is triggered every `icount_checking_period` or
 * `quantum_checking_period` cycles. Cycles are calculated from the provided IPC
 * and the instruction count.
 */
CYAN_API bool
qemu_plugin_register_periodic_check_cb(qemu_plugin_periodic_check_cb_t cb);

/**
 * The following functions are used to get and set the virtual time of a
 * specific vCPU.
 *
 * This part of the logic should be able to be implemented by the plugin itself.
 *
 * The unit, unfortunately, is centi-cycle.
 */
CYAN_API uint64_t qemu_plugin_get_vcpu_vtime(uint32_t cpu_idx);
CYAN_API void qemu_plugin_set_vcpu_vtime(uint32_t cpu_idx, uint64_t vtime);
CYAN_API uint64_t qemu_plugin_get_vcpu_ipc(uint32_t cpu_idx);

// typedef struct qemu_per_cpu_exchangable_state_t {
//   uint64_t ipc;
//   uint64_t *vtime;
//   uint64_t *pc;
//   uint64_t *ttbr0_el1;
//   uint64_t *ttbr1_el1;
//   uint64_t *tcr_el1;
//   uint64_t *xregs;
// } qemu_per_cpu_exchangable_state_t;


enum qemu_plugin_tlb_flush_type_t {
  QEMU_PLUGIN_TLB_FLUSH_ALL = 0,
  QEMU_PLUGIN_TLB_FLUSH_BY_ASID = 1,
  QEMU_PLUGIN_TLB_FLUSH_BY_VPN = 2,
  QEMU_PLUGIN_TLB_FLUSH_BY_ASID_AND_VPN = 3,
};

typedef void (*qemu_plugin_flushing_local_tlb_t)(
    uint32_t vcpu_idx,
    enum qemu_plugin_tlb_flush_type_t mode,
    uint64_t asid,
    uint64_t vpn,
    uint64_t number_of_pages
);

CYAN_API bool qemu_plugin_register_flushing_local_tlb_cb(
    qemu_plugin_flushing_local_tlb_t cb);

#endif /* QEMU_QEMU_PLUGIN_H */
