// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include <lib/zxio/ops.h>
#include <lib/zxio/zxio.h>
#include <zircon/syscalls.h>

// The private fields of a |zxio_t| object.
//
// In |ops.h|, the |zxio_t| struct is defined as opaque. Clients of the zxio
// library are forbidden from relying upon the structure of |zxio_t| objects.
// To avoid temptation, the details of the structure are defined only in this
// implementation file and are not visible in the header.
typedef struct zxio_internal {
  const zxio_ops_t* ops;
  uint64_t reserved[3];
} zxio_internal_t;

static_assert(sizeof(zxio_t) == sizeof(zxio_internal_t), "zxio_t should match zxio_internal_t");

typedef struct zxio_dirent_iterator_internal {
  zxio_t* io;
  uint8_t* buffer;
  uint64_t capacity;
  uint64_t count;
  uint64_t index;
  uint64_t opaque[3];
} zxio_dirent_iterator_internal_t;

static_assert(sizeof(zxio_dirent_iterator_t) == sizeof(zxio_dirent_iterator_internal_t),
              "zxio_dirent_iterator_t should match zxio_dirent_iterator_internal_t");

static_assert(ZXIO_READABLE == __ZX_OBJECT_READABLE, "ZXIO signal bits should match ZX");
static_assert(ZXIO_WRITABLE == __ZX_OBJECT_WRITABLE, "ZXIO signal bits should match ZX");
static_assert(ZXIO_READ_DISABLED == ZX_SOCKET_PEER_WRITE_DISABLED,
              "ZXIO signal bits should match ZX");
static_assert(ZXIO_WRITE_DISABLED == ZX_SOCKET_WRITE_DISABLED, "ZXIO signal bits should match ZX");
static_assert(ZXIO_READ_THRESHOLD == ZX_SOCKET_READ_THRESHOLD, "ZXIO signal bits should match ZX");
static_assert(ZXIO_WRITE_THRESHOLD == ZX_SOCKET_WRITE_THRESHOLD,
              "ZXIO signal bits should match ZX");

void zxio_init(zxio_t* io, const zxio_ops_t* ops) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  memset(zio, 0, sizeof(*zio));
  zio->ops = ops;
}

zx_status_t zxio_release(zxio_t* io, zx_handle_t* out_handle) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->release(io, out_handle);
}

zx_status_t zxio_clone(zxio_t* io, zx_handle_t* out_handle) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->clone(io, out_handle);
}

zx_status_t zxio_close(zxio_t* io) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->close(io);
}

zx_status_t zxio_wait_one(zxio_t* io, zxio_signals_t signals, zx_time_t deadline,
                          zxio_signals_t* out_observed) {
  zx_handle_t handle = ZX_HANDLE_INVALID;
  zx_signals_t zx_signals = ZX_SIGNAL_NONE;
  zxio_wait_begin(io, signals, &handle, &zx_signals);
  if (handle == ZX_HANDLE_INVALID) {
    return ZX_ERR_NOT_SUPPORTED;
  }
  zx_signals_t zx_observed = ZX_SIGNAL_NONE;
  zx_status_t status = zx_object_wait_one(handle, zx_signals, deadline, &zx_observed);
  if (status != ZX_OK) {
    return status;
  }
  zxio_wait_end(io, zx_signals, out_observed);
  return ZX_OK;
}

void zxio_wait_begin(zxio_t* io, zxio_signals_t zxio_signals, zx_handle_t* out_handle,
                     zx_signals_t* out_zx_signals) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->wait_begin(io, zxio_signals, out_handle, out_zx_signals);
}

void zxio_wait_end(zxio_t* io, zx_signals_t zx_signals, zxio_signals_t* out_zxio_signals) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->wait_end(io, zx_signals, out_zxio_signals);
}

zx_status_t zxio_sync(zxio_t* io) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->sync(io);
}

zx_status_t zxio_attr_get(zxio_t* io, zxio_node_attr_t* out_attr) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->attr_get(io, out_attr);
}

zx_status_t zxio_attr_set(zxio_t* io, uint32_t flags, const zxio_node_attr_t* attr) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->attr_set(io, flags, attr);
}

zx_status_t zxio_read(zxio_t* io, void* buffer, size_t capacity, zxio_flags_t flags,
                      size_t* out_actual) {
  const zx_iovec_t vector = {
      .buffer = buffer,
      .capacity = capacity,
  };
  return zxio_read_vector(io, &vector, 1, flags, out_actual);
}

zx_status_t zxio_read_at(zxio_t* io, zx_off_t offset, void* buffer, size_t capacity,
                         zxio_flags_t flags, size_t* out_actual) {
  const zx_iovec_t vector = {
      .buffer = buffer,
      .capacity = capacity,
  };
  return zxio_read_vector_at(io, offset, &vector, 1, flags, out_actual);
}

zx_status_t zxio_write(zxio_t* io, const void* buffer, size_t capacity, zxio_flags_t flags,
                       size_t* out_actual) {
  const zx_iovec_t vector = {
      .buffer = const_cast<void*>(buffer),
      .capacity = capacity,
  };
  return zxio_write_vector(io, &vector, 1, flags, out_actual);
}

zx_status_t zxio_write_at(zxio_t* io, zx_off_t offset, const void* buffer, size_t capacity,
                          zxio_flags_t flags, size_t* out_actual) {
  const zx_iovec_t vector = {
      .buffer = const_cast<void*>(buffer),
      .capacity = capacity,
  };
  return zxio_write_vector_at(io, offset, &vector, 1, flags, out_actual);
}

zx_status_t zxio_read_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                             zxio_flags_t flags, size_t* out_actual) {
  auto zio = (zxio_internal_t*)io;
  return zio->ops->read_vector(io, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_read_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  auto zio = (zxio_internal_t*)io;
  return zio->ops->read_vector_at(io, offset, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_write_vector(zxio_t* io, const zx_iovec_t* vector, size_t vector_count,
                              zxio_flags_t flags, size_t* out_actual) {
  auto zio = (zxio_internal_t*)io;
  return zio->ops->write_vector(io, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_write_vector_at(zxio_t* io, zx_off_t offset, const zx_iovec_t* vector,
                                 size_t vector_count, zxio_flags_t flags, size_t* out_actual) {
  auto zio = (zxio_internal_t*)io;
  return zio->ops->write_vector_at(io, offset, vector, vector_count, flags, out_actual);
}

zx_status_t zxio_seek(zxio_t* io, zx_off_t offset, zxio_seek_origin_t start, size_t* out_offset) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->seek(io, offset, start, out_offset);
}

zx_status_t zxio_truncate(zxio_t* io, size_t length) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->truncate(io, length);
}

zx_status_t zxio_flags_get(zxio_t* io, uint32_t* out_flags) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->flags_get(io, out_flags);
}

zx_status_t zxio_flags_set(zxio_t* io, uint32_t flags) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->flags_set(io, flags);
}

zx_status_t zxio_token_get(zxio_t* io, zx_handle_t* out_token) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->token_get(io, out_token);
}

zx_status_t zxio_vmo_get(zxio_t* io, uint32_t flags, zx_handle_t* out_vmo, size_t* out_size) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->vmo_get(io, flags, out_vmo, out_size);
}

zx_status_t zxio_vmo_get_copy(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_vmo_get_clone(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_vmo_get_exact(zxio_t* io, zx_handle_t* out_vmo, size_t* out_size) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_open(zxio_t* directory, uint32_t flags, uint32_t mode, const char* path,
                      zxio_t** out_io) {
  zxio_internal_t* zio = (zxio_internal_t*)directory;
  return zio->ops->open(directory, flags, mode, path, out_io);
}

zx_status_t zxio_open_async(zxio_t* directory, uint32_t flags, uint32_t mode, const char* path,
                            size_t path_len, zx_handle_t request) {
  zxio_internal_t* zio = (zxio_internal_t*)directory;
  return zio->ops->open_async(directory, flags, mode, path, path_len, request);
}

zx_status_t zxio_unlink(zxio_t* directory, const char* path) {
  zxio_internal_t* zio = (zxio_internal_t*)directory;
  return zio->ops->unlink(directory, path);
}

zx_status_t zxio_rename(zxio_t* old_directory, const char* old_path,
                        zx_handle_t new_directory_token, const char* new_path) {
  zxio_internal_t* zio = (zxio_internal_t*)old_directory;
  return zio->ops->rename(old_directory, old_path, new_directory_token, new_path);
}

zx_status_t zxio_link(zxio_t* src_directory, const char* src_path, zxio_t* dst_directory,
                      const char* dst_path) {
  return ZX_ERR_NOT_SUPPORTED;
}

zx_status_t zxio_dirent_iterator_init(zxio_dirent_iterator_t* iterator, zxio_t* directory,
                                      void* buffer, size_t capacity) {
  zxio_internal_t* zio = (zxio_internal_t*)directory;

  zxio_dirent_iterator_internal_t* iter = (zxio_dirent_iterator_internal_t*)iterator;
  iter->io = directory;
  iter->buffer = reinterpret_cast<uint8_t*>(buffer);
  iter->capacity = capacity;
  iter->count = 0;
  iter->index = 0;

  if (!zio->ops->readdir) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  return ZX_OK;
}

zx_status_t zxio_dirent_iterator_next(zxio_dirent_iterator_t* iterator, zxio_dirent_t** out_entry) {
  zxio_dirent_iterator_internal_t* iter = (zxio_dirent_iterator_internal_t*)iterator;
  zxio_internal_t* zio = (zxio_internal_t*)iter->io;

  if (!zio->ops->readdir) {
    return ZX_ERR_NOT_SUPPORTED;
  }

  if (iter->index >= iter->count) {
    size_t count;
    zx_status_t status = zio->ops->readdir(iter->io, iter->buffer, iter->capacity, &count);
    if (status != ZX_OK) {
      return status;
    }

    if (count == 0) {
      return ZX_ERR_NOT_FOUND;
    }

    iter->count = count;
    iter->index = 0;
  }

  auto entry = reinterpret_cast<zxio_dirent_t*>(&iter->buffer[iter->index]);

  // Check if we can read the entry size.
  if (iter->index + sizeof(zxio_dirent_t) > iter->count) {
    // Should not happen
    return ZX_ERR_INTERNAL;
  }

  size_t entry_size = sizeof(zxio_dirent_t) + entry->size;

  // Check if we can read the whole entry.
  if (iter->index + entry_size > iter->count) {
    // Should not happen
    return ZX_ERR_INTERNAL;
  }

  iter->index += entry_size;
  *out_entry = entry;

  return ZX_OK;
}

void zxio_dirent_iterator_destroy(zxio_dirent_iterator_t* iterator) {
  zxio_dirent_iterator_internal_t* iter = (zxio_dirent_iterator_internal_t*)iterator;
  zxio_internal_t* zio = (zxio_internal_t*)iter->io;
  if (!zio->ops->readdir) {
    return;
  }
  zio->ops->rewind(iter->io);
}

zx_status_t zxio_isatty(zxio_t* io, bool* tty) {
  zxio_internal_t* zio = (zxio_internal_t*)io;
  return zio->ops->isatty(io, tty);
}
