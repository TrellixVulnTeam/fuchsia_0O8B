// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be found in the LICENSE file.

#ifndef ZIRCON_SYSTEM_ULIB_FS_JOURNAL_TEST_FUZZER_UTILS_H_
#define ZIRCON_SYSTEM_ULIB_FS_JOURNAL_TEST_FUZZER_UTILS_H_

#include <lib/zx/vmo.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
#include <zircon/assert.h>
#include <zircon/device/block.h>

#include <map>

#include <fs/journal/format.h>
#include <fs/journal/superblock.h>
#include <fs/transaction/block_transaction.h>
#include <fuzzer/FuzzedDataProvider.h>
#include <storage/buffer/blocking-ring-buffer.h>
#include <storage/buffer/vmo-buffer.h>
#include <storage/buffer/vmoid-registry.h>
#include <storage/operation/buffered-operation.h>
#include <storage/operation/operation.h>

namespace fs {

enum class ReservedVmoid : vmoid_t {
  kInfoVmoid,
  kJournalVmoid,
  kWritebackVmoid,
  kDataVmoid,
  kMaxReserved,
};

class FuzzerUtils;

// Fake that allows callers to directly interact with VMOs that would normally be passed to storage
// devices. It allows the caller to set the keys used to map the VMOs and retrieve them as needed.
class FuzzedVmoidRegistry final : public storage::VmoidRegistry {
 public:
  FuzzedVmoidRegistry() = default;
  ~FuzzedVmoidRegistry() = default;

  bool HasVmo(vmoid_t vmoid) const { return vmos_.find(vmoid) != vmos_.end(); }
  bool HasVmo(ReservedVmoid vmoid) const { return HasVmo(static_cast<vmoid_t>(vmoid)); }

  const zx::vmo& GetVmo(vmoid_t vmoid) const { return *(vmos_.at(vmoid)); }
  const zx::vmo& GetVmo(ReservedVmoid vmoid) const { return GetVmo(static_cast<vmoid_t>(vmoid)); }

  // Sets the vmoid that will be returned by the next call to |AttachVmo|.
  void SetNextVmoid(vmoid_t vmoid) { next_vmoid_ = vmoid; }
  void SetNextVmoid(ReservedVmoid vmoid) { SetNextVmoid(static_cast<vmoid_t>(vmoid)); }

  // storage::VmoidRegistry interface
  zx_status_t AttachVmo(const zx::vmo& vmo, vmoid_t* out) final;
  zx_status_t DetachVmo(vmoid_t vmoid) final;

 private:
  vmoid_t next_vmoid_ = BLOCK_VMOID_INVALID;
  std::map<vmoid_t, zx::unowned_vmo> vmos_;
};

// Fake that returns data from the fuzzer rather than from disk.
class FuzzedTransactionHandler final : public fs::TransactionHandler {
 public:
  FuzzedTransactionHandler() = default;
  ~FuzzedTransactionHandler() = default;

  // Register the parent object, which provides the FuzzedDataProvider.
  void Init(FuzzerUtils* fuzz_utils);

  // Indicate the block number that the journal should start at. Read transactions at this offset
  // will return a fuzzed superblock.
  void SetJournalStart(uint64_t journal_start) { journal_start_ = journal_start; }

  // TransactionHandler interface
  uint32_t FsBlockSize() const final { return block_size_; }
  groupid_t BlockGroupID() final { return 1; }
  uint32_t DeviceBlockSize() const final { return block_size_; }
  uint64_t BlockNumberToDevice(uint64_t block_num) const final { return block_num; }
  block_client::BlockDevice* GetDevice() final { return nullptr; }
  zx_status_t RunOperation(const storage::Operation& operation, storage::BlockBuffer* buffer) final;
  zx_status_t Transaction(block_fifo_request_t* requests, size_t count) final;

 private:
  FuzzerUtils* fuzz_utils_ = nullptr;
  uint32_t block_size_ = 0;
  uint64_t journal_start_ = 0;
};

// A collection of utilities to plumb fuzzed data through a fake, journaled device.
class FuzzerUtils final {
 public:
  FuzzerUtils(const uint8_t* data, size_t size) : input_(data, size) { handler_.Init(this); }
  ~FuzzerUtils() = default;

  // Returns the block size. Guaranteed to be a power of 2 between 512 and 32k.
  uint32_t block_size() const { return handler_.FsBlockSize(); }

  FuzzedDataProvider* data_provider() { return &input_; }
  FuzzedVmoidRegistry* registry() { return &registry_; }
  FuzzedTransactionHandler* handler() { return &handler_; }

  // Creates and returns a properly registered, VMO-backed ring buffer.
  zx_status_t CreateRingBuffer(const char* label, ReservedVmoid vmoid, size_t len,
                               std::unique_ptr<storage::BlockingRingBuffer>* out);

  zx_status_t FuzzSuperblock(JournalSuperblock* out_info);
  zx_status_t FuzzJournal(storage::VmoBuffer* out_journal);
  fbl::Vector<storage::UnbufferedOperation> FuzzOperation(ReservedVmoid vmoid);

 private:
  FuzzedDataProvider input_;
  FuzzedVmoidRegistry registry_;
  FuzzedTransactionHandler handler_;
};

}  // namespace fs

#endif  // ZIRCON_SYSTEM_ULIB_FS_JOURNAL_TEST_FUZZER_UTILS_H_
