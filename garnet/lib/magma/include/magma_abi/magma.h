// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

// NOTE: DO NOT EDIT THIS FILE!
// It is automatically generated by //garnet/lib/magma/include/magma_abi/magma.h.gen.py

// clang-format off

#ifndef GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_
#define GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_

#include "magma_common_defs.h"
#include <stdint.h>

#if defined(__cplusplus)
extern "C" {
#endif

///
/// \brief Releases the given connection.
/// \param connection An open connection.
///
void magma_release_connection(
    magma_connection_t connection);

///
/// \brief Returns the first recorded error since the last time this function was called, and clears
///        the recorded error. Incurs a round-trip to the system driver
/// \param connection An open connection.
///
magma_status_t magma_get_error(
    magma_connection_t connection);

///
/// \brief Creates a context on the given connection.
/// \param connection An open connection.
/// \param context_id_out The returned context id.
///
void magma_create_context(
    magma_connection_t connection,
    uint32_t* context_id_out);

///
/// \brief Releases the context associated with the given id.
/// \param connection An open connection.
/// \param context_id A valid context id.
///
void magma_release_context(
    magma_connection_t connection,
    uint32_t context_id);

///
/// \brief Creates a memory buffer of at least the given size.
/// \param connection An open connection.
/// \param size Requested buffer size.
/// \param size_out The returned buffer's actual size.
/// \param buffer_out The returned buffer.
///
magma_status_t magma_create_buffer(
    magma_connection_t connection,
    uint64_t size,
    uint64_t* size_out,
    magma_buffer_t* buffer_out);

///
/// \brief Releases the given memory buffer.
/// \param connection An open connection.
/// \param buffer A valid buffer.
///
void magma_release_buffer(
    magma_connection_t connection,
    magma_buffer_t buffer);

///
/// \brief Duplicates the given handle, giving another handle that can be imported into a
///        connection.
/// \param buffer_handle A valid handle.
/// \param buffer_handle_out The returned duplicate handle.
///
magma_status_t magma_duplicate_handle(
    magma_handle_t buffer_handle,
    magma_handle_t* buffer_handle_out);

///
/// \brief Releases the given handle.
/// \param buffer_handle A valid handle.
///
magma_status_t magma_release_buffer_handle(
    magma_handle_t buffer_handle);

///
/// \brief Returns a unique id for the given buffer.
/// \param buffer A valid buffer.
///
uint64_t magma_get_buffer_id(
    magma_buffer_t buffer);

///
/// \brief Returns the actual size of the given buffer.
/// \param buffer A valid buffer.
///
uint64_t magma_get_buffer_size(
    magma_buffer_t buffer);

///
/// \brief Cleans, and optionally invalidates, the cache for the region of memory specified by the
///        given buffer, offset, and size, and write the contents to ram.
/// \param buffer A valid buffer.
/// \param offset An offset into the buffer. Must be less than or equal to the buffer's size.
/// \param size Size of region to be cleaned. Offset + size must be less than or equal to the
///        buffer's size.
/// \param operation One of MAGMA_CACHE_OPERATION_CLEAN or MAGMA_CACHE_OPERATION_CLEAN_INVALIDATE.
///
magma_status_t magma_clean_cache(
    magma_buffer_t buffer,
    uint64_t offset,
    uint64_t size,
    magma_cache_operation_t operation);

///
/// \brief Configures the cache for the given buffer.
/// \param buffer A valid buffer.
/// \param policy One of MAGMA_CACHE_POLICY_[CACHED|WRITE_COMBINING|UNCACHED].
///
magma_status_t magma_set_cache_policy(
    magma_buffer_t buffer,
    magma_cache_policy_t policy);

///
/// \brief Queries the cache policy for a buffer.
/// \param buffer A valid buffer.
/// \param cache_policy_out The returned cache policy.
///
magma_status_t magma_get_buffer_cache_policy(
    magma_buffer_t buffer,
    magma_cache_policy_t* cache_policy_out);

///
/// \brief Queries whether a buffer is mappable with magma_map or magma_map_aligned
/// \param buffer The given buffer.
/// \param flags Reserved for future use. Must be 0.
/// \param is_mappable_out The returned mappability value.
///
magma_status_t magma_get_buffer_is_mappable(
    magma_buffer_t buffer,
    uint32_t flags,
    magma_bool_t* is_mappable_out);

///
/// \brief Sets the mapping address range on the given buffer.  Ownership of the handle is
///        transferred to magma.
/// \param buffer The given buffer.
/// \param handle A platform specific handle to the address range.
///
magma_status_t magma_set_buffer_mapping_address_range(
    magma_buffer_t buffer,
    magma_handle_t handle);

///
/// \brief Maps the given buffer's memory into the calling process's address space.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param addr_out The returned CPU virtual address for the start of the buffer.
///
magma_status_t magma_map(
    magma_connection_t connection,
    magma_buffer_t buffer,
    void** addr_out);

///
/// \brief Maps the given buffer's memory into the calling process's address space with a given
///        alignment.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param alignment The requested alignment. Must be a power of 2 and at least PAGE_SIZE.
/// \param addr_out The returned CPU virtual address for the start of the buffer.
///
magma_status_t magma_map_aligned(
    magma_connection_t connection,
    magma_buffer_t buffer,
    uint64_t alignment,
    void** addr_out);

///
/// \brief Maps a region of the given buffer's memory at the given CPU virtual address. Fails if the
///        buffer was previously mapped, or if the address range is unavailable.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param addr A valid CPU virtual address.
/// \param offset Offset into the buffer. Must be less than or equal to the buffer's size.
/// \param length Size of region to be mapped. Offset + length must be less than or equal to the
///        buffer's size.
///
magma_status_t magma_map_specific(
    magma_connection_t connection,
    magma_buffer_t buffer,
    uint64_t addr,
    uint64_t offset,
    uint64_t length);

///
/// \brief Releases a CPU mapping for the given buffer. Should be paired with each call to one of
///        the mapping interfaces.
/// \param connection An open connection.
/// \param buffer A valid buffer with at least one active CPU mapping.
///
magma_status_t magma_unmap(
    magma_connection_t connection,
    magma_buffer_t buffer);

///
/// \brief Maps a number of pages from the given buffer onto the GPU in the connection's address
///        space at the given address.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param page_offset Offset into the buffer in pages.
/// \param page_count Number of pages to map.
/// \param gpu_va Destination GPU virtual address for the mapping.
/// \param map_flags A valid MAGMA_GPU_MAP_FLAGS value.
///
void magma_map_buffer_gpu(
    magma_connection_t connection,
    magma_buffer_t buffer,
    uint64_t page_offset,
    uint64_t page_count,
    uint64_t gpu_va,
    uint64_t map_flags);

///
/// \brief Releases the mapping at the given address from the GPU.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param gpu_va A GPU virtual address associated with an existing mapping of the given buffer.
///
void magma_unmap_buffer_gpu(
    magma_connection_t connection,
    magma_buffer_t buffer,
    uint64_t gpu_va);

///
/// \brief Ensures that the given page range of a buffer is backed by physical memory.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param page_offset Page offset into the buffer.
/// \param page_count Number of pages to commit.
///
magma_status_t magma_commit_buffer(
    magma_connection_t connection,
    magma_buffer_t buffer,
    uint64_t page_offset,
    uint64_t page_count);

///
/// \brief Exports the given buffer, returning a handle that may be imported into another
///        connection.
/// \param connection An open connection.
/// \param buffer A valid buffer.
/// \param buffer_handle_out The returned handle.
///
magma_status_t magma_export(
    magma_connection_t connection,
    magma_buffer_t buffer,
    magma_handle_t* buffer_handle_out);

///
/// \brief Imports and takes ownership of the buffer referred to by the given handle.
/// \param connection An open connection.
/// \param buffer_handle A valid handle.
/// \param buffer_out The returned buffer.
///
magma_status_t magma_import(
    magma_connection_t connection,
    magma_handle_t buffer_handle,
    magma_buffer_t* buffer_out);

///
/// \brief Submits a command buffer for execution on the GPU, with associated resources.
/// \param connection An open connection.
/// \param context_id A valid context ID.
/// \param command_buffer A pointer to the command buffer to execute.
/// \param resources An array of |command_buffer->num_resources| resources associated with the
///        command buffer.
/// \param semaphore_ids An array of semaphore ids; first should be
///        |command_buffer->wait_semaphore_count| wait semaphores followed by
///        |command_buffer->signal_signal_semaphores| signal semaphores.
///
void magma_execute_command_buffer_with_resources(
    magma_connection_t connection,
    uint32_t context_id,
    struct magma_system_command_buffer* command_buffer,
    struct magma_system_exec_resource* resources,
    uint64_t* semaphore_ids);

///
/// \brief Submits a series of commands for execution on the GPU without using a command buffer.
/// \param connection An open connection.
/// \param context_id A valid context ID.
/// \param command_count The number of commands in the provided buffer.
/// \param command_buffers An array of command_count magma_inline_command_buffer structs.
///
void magma_execute_immediate_commands2(
    magma_connection_t connection,
    uint32_t context_id,
    uint64_t command_count,
    struct magma_inline_command_buffer* command_buffers);

///
/// \brief Creates a semaphore.
/// \param connection An open connection.
/// \param semaphore_out The returned semaphore.
///
magma_status_t magma_create_semaphore(
    magma_connection_t connection,
    magma_semaphore_t* semaphore_out);

///
/// \brief Releases the given semaphore.
/// \param connection An open connection.
/// \param semaphore A valid semaphore.
///
void magma_release_semaphore(
    magma_connection_t connection,
    magma_semaphore_t semaphore);

///
/// \brief Returns a unique id for the given semaphore.
/// \param semaphore A valid semaphore.
///
uint64_t magma_get_semaphore_id(
    magma_semaphore_t semaphore);

///
/// \brief Signals the given semaphore.
/// \param semaphore A valid semaphore.
///
void magma_signal_semaphore(
    magma_semaphore_t semaphore);

///
/// \brief Resets the given semaphore.
/// \param semaphore A valid semaphore.
///
void magma_reset_semaphore(
    magma_semaphore_t semaphore);

///
/// \brief Waits for one or all provided semaphores to be signaled. Does not reset any semaphores.
/// \param semaphores Array of valid semaphores.
/// \param count Number of semaphores in the array.
/// \param timeout_ms Time to wait before returning MAGMA_STATUS_TIMED_OUT.
/// \param wait_all If true, wait for all semaphores, otherwise wait for any semaphore.
///
magma_status_t magma_wait_semaphores(
    const magma_semaphore_t* semaphores,
    uint32_t count,
    uint64_t timeout_ms,
    magma_bool_t wait_all);

///
/// \brief Exports the given semaphore, returning a handle that may be imported into another
///        connection
/// \param connection An open connection.
/// \param semaphore A valid semaphore.
/// \param semaphore_handle_out The returned handle.
///
magma_status_t magma_export_semaphore(
    magma_connection_t connection,
    magma_semaphore_t semaphore,
    magma_handle_t* semaphore_handle_out);

///
/// \brief Imports and takes ownership of the semaphore referred to by the given handle.
/// \param connection An open connection.
/// \param semaphore_handle A valid semaphore handle.
/// \param semaphore_out The returned semaphore.
///
magma_status_t magma_import_semaphore(
    magma_connection_t connection,
    magma_handle_t semaphore_handle,
    magma_semaphore_t* semaphore_out);

///
/// \brief Returns a handle that can be waited on to determine when the connection has data in the
///        notification channel. This channel has the same lifetime as the connection and must not
///        be closed by the client.
/// \param connection An open connection.
///
magma_handle_t magma_get_notification_channel_handle(
    magma_connection_t connection);

///
/// \brief Returns MAGMA_STATUS_OK if a message is available on the notification channel before the
///        given timeout expires.
/// \param connection An open connection.
/// \param timeout_ns Time to wait before returning MAGMA_STATUS_TIMED_OUT.
///
magma_status_t magma_wait_notification_channel(
    magma_connection_t connection,
    int64_t timeout_ns);

///
/// \brief Reads a notification from the channel into the given buffer.
/// \param connection An open connection.
/// \param buffer Buffer into which to read notification data.
/// \param buffer_size Size of the given buffer.
/// \param buffer_size_out Returned size of the notification data written to the buffer, or 0 if
///        there are no messages pending.
///
magma_status_t magma_read_notification_channel(
    magma_connection_t connection,
    void* buffer,
    uint64_t buffer_size,
    uint64_t* buffer_size_out);

///
/// \brief Initializes tracing
/// \param channel An open connection to a tracing provider.
///
magma_status_t magma_initialize_tracing(
    magma_handle_t channel);

///
/// \brief Imports and takes ownership of a channel to a device.
/// \param device_channel A channel connecting to a gpu class device.
/// \param device_out Returned device.
///
magma_status_t magma_device_import(
    magma_handle_t device_channel,
    magma_device_t* device_out);

///
/// \brief Releases a handle to a device
/// \param device An open device.
///
void magma_device_release(
    magma_device_t device);

///
/// \brief Performs a query and returns a result synchronously.
/// \param device An open device.
/// \param id Either MAGMA_QUERY_DEVICE_ID, or a vendor-specific id starting from
///        MAGMA_QUERY_VENDOR_PARAM_0.
/// \param value_out Returned value.
///
magma_status_t magma_query2(
    magma_device_t device,
    uint64_t id,
    uint64_t* value_out);

///
/// \brief Performs a query for a large amount of data and puts that into a buffer. Returns
///        synchronously
/// \param device An open device.
/// \param id A vendor-specific ID.
/// \param handle_out Handle to the returned buffer.
///
magma_status_t magma_query_returns_buffer2(
    magma_device_t device,
    uint64_t id,
    uint32_t* handle_out);

///
/// \brief Opens a connection to a device.
/// \param device An open device
/// \param connection_out Returned connection.
///
magma_status_t magma_create_connection2(
    magma_device_t device,
    magma_connection_t* connection_out);

///
/// \brief Initializes logging; used for debug and some exceptional error reporting.
/// \param channel An open connection to the syslog service.
///
magma_status_t magma_initialize_logging(
    magma_handle_t channel);

#if defined(__cplusplus)
}
#endif

#endif // GARNET_LIB_MAGMA_INCLUDE_MAGMA_ABI_MAGMA_H_
