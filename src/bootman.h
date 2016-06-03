/*
 * This file is part of clr-boot-manager.
 *
 * Copyright © 2016 Intel Corporation
 *
 * clr-boot-manager is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public License as
 * published by the Free Software Foundation; either version 2.1
 * of the License, or (at your option) any later version.
 */

#pragma once

#include "nica/array.h"
#include "util.h"

typedef struct BootManager BootManager;

typedef enum {
        BOOTLOADER_OPERATION_MIN = 0,
        BOOTLOADER_OPERATION_REMOVE,
        BOOTLOADER_OPERATION_INSTALL,
        BOOTLOADER_OPERATION_UPDATE,
        BOOTLOADER_OPERATION_NO_CHECK,
        BOOTLOADER_OPERATION_MAX
} BootLoaderOperation;

/**
 * Represents a kernel in it's complete configuration
 */
typedef struct Kernel {
        char *path;         /**<Path to this kernel */
        char *bpath;        /**<Basename of this kernel path */
        char *version;      /**<Version of this kernel */
        int16_t release;    /**<Release number of this kernel */
        char *ktype;        /**<Type of this kernel */
        char *cmdline;      /**<Contents of the cmdline file */
        char *cmdline_file; /**<Path to the cmdline file */
        char *kconfig_file; /**<Path to the kconfig file */
        char *module_dir;   /**<Path to the modules directory */
        bool is_running;    /**<Is this the running kernel? */
        bool boots;         /**<Is this known to boot? */
        char *kboot_file;   /**<Path to the k_booted_$(uname -r) file */
} Kernel;

typedef NcArray KernelArray;

/**
 * Construct a new BootManager
 *
 * @note In no way is this API supposed to be used in a parallel fashion.
 * The bootloader implementation is reliant on a static config in an extern
 * module, and we do not create "new" instances here. We simply encapsulate
 * much through an OOP-like interface to make relevant bits available
 * throughout the architecture.
 */
BootManager *boot_manager_new(void);

/**
 * Free an already constructed BootManager
 */
void boot_manager_free(BootManager *manager);

/**
 * Set the prefix to apply to all filesystem paths
 *
 * @note The path must exist for this function call to work
 *
 * @param prefix Path to use for prefix operations
 *
 * @return True if the operation succeeded
 */
bool boot_manager_set_prefix(BootManager *manager, char *prefix);

/**
 * Override the internal boot directory, forcing a reconfiguration.
 * This should only be used if the client has no need to mount the configured
 * ESP, i.e. because it is already mounted by the user
 */
bool boot_manager_set_boot_dir(BootManager *manager, const char *bootdir);

/**
 * Get the current filesystem prefix
 *
 * @note This string is owned by BootManager, do not modify or free
 *
 * @return current prefix
 */
const char *boot_manager_get_prefix(BootManager *manager);

/**
 * Return the location used for kernel probing, incorporating the
 * prefix
 *
 * @note This string is owned by BootManager, do not modify or free
 *
 * @return current kernel directory
 */
const char *boot_manager_get_kernel_dir(BootManager *manager);

/**
 * Set the vendor prefix for all relevant files
 */
void boot_manager_set_vendor_prefix(BootManager *manager, char *vendor_prefix);

/**
 * Return the vendor prefix, if any
 *
 * @note This string is owned by BootManager, do not modify or free
 */
const char *boot_manager_get_vendor_prefix(BootManager *manager);

/**
 * Set the OS name
 */
void boot_manager_set_os_name(BootManager *manager, char *os_name);

/**
 * Return the OS name
 *
 * @note This string is owned by BootManager, do not modify or free
 */
const char *boot_manager_get_os_name(BootManager *manager);

/**
 * Discover a list of known kernels
 *
 * @return a newly allocated NcArray of Kernel's
 */
KernelArray *boot_manager_get_kernels(BootManager *manager);

/**
 * Inspect a kernel file, returning a machine-usable description
 *
 * @note free the returned struct with free_kernel
 *
 * @param path Path of the file to inspect
 * @return a newly allocated Kernel, or NULL if this is an invalid kernel
 */
Kernel *boot_manager_inspect_kernel(BootManager *manager, char *path);

/**
 * Attempt installation of the given kernel
 *
 * @param kernel A valid kernel instance
 *
 * @return a boolean value, indicating success of failure
 */
bool boot_manager_install_kernel(BootManager *manager, const Kernel *kernel);

/**
 * Return the fully qualified boot directory, including the prefix
 */
char *boot_manager_get_boot_dir(BootManager *manager);

/**
 * Attempt to uninstall a previously installed kernel
 *
 * @param kernel A valid kernel instance
 *
 * @return a boolean value, indicating success or failure
 */

bool boot_manager_remove_kernel(BootManager *manager, const Kernel *kernel);

/**
 * Attempt to set the default kernel entry
 */
bool boot_manager_set_default_kernel(BootManager *manager, const Kernel *kernel);

/**
 * Return the PartUUID for the root partition
 *
 * @note This string belongs to BootManager and should not be freed. Also
 * note that it is only initialised during @boot_manager_set_prefix
 */
const char *boot_manager_get_root_uuid(BootManager *manager);

/**
 * Attempt installation of the bootloader
 */
bool boot_manager_modify_bootloader(BootManager *manager, BootLoaderOperation op);

/**
 * Determine the firmware architecture
 *
 * @note If the firmware fails to expose this information, or indeed the
 * /sys/firmware/efi path is unavailable due to an image build, we'll return
 * the architecture size instead.
 *
 * @return Either 32-bit or 64-bit
 */
uint8_t boot_manager_get_platform_size(BootManager *manager);

/**
 * Determine the architecture of this running process
 *
 * @note The distinction is important for multilib and CONFIG_EFI_MIXED
 * environments, we must know the native architecture vs the firmware
 * architecture
 */
uint8_t boot_manager_get_architecture_size(BootManager *manager);

/**
 * Determine if the BootManager is operating in image mode, i.e.
 * the prefix/root is not "/" - the native filesystem
 */
bool boot_manager_is_image_mode(BootManager *manager);

/**
 * Determine the default timeout based on the contents of
 * SYSCONFDIR/boot_timeout
 */
int boot_manager_get_timeout_value(BootManager *manager);

/**
 * Determine the default kernel for the given type if it is in the set
 * This does not create a new instance, simply a pointer to the existing
 * kernel in the @kernels set.
 */
Kernel *boot_manager_get_default_for_type(BootManager *manager, KernelArray *kernels, char *type);

/**
 * Set the timeout to be used in the bootloader
 *
 * @param timeout New timeout value
 */
bool boot_manager_set_timeout_value(BootManager *manager, int timeout);

bool boot_manager_needs_install(BootManager *manager);

bool boot_manager_needs_update(BootManager *manager);

bool boot_manager_is_kernel_installed(BootManager *manager, const Kernel *kernel);
/**
 * Free a kernel type
 */
void free_kernel(Kernel *t);

static inline void kernel_array_free(void *v)
{
        KernelArray *a = v;
        nc_array_free(&a, (array_free_func)free_kernel);
}

DEF_AUTOFREE(BootManager, boot_manager_free)
DEF_AUTOFREE(KernelArray, kernel_array_free)
DEF_AUTOFREE(Kernel, free_kernel)

/*
 * Editor modelines  -  https://www.wireshark.org/tools/modelines.html
 *
 * Local variables:
 * c-basic-offset: 8
 * tab-width: 8
 * indent-tabs-mode: nil
 * End:
 *
 * vi: set shiftwidth=8 tabstop=8 expandtab:
 * :indentSize=8:tabSize=8:noTabs=true:
 */
