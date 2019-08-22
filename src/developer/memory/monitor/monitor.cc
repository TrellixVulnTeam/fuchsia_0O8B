// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/developer/memory/monitor/monitor.h"

#include <errno.h>
#include <fcntl.h>
#include <lib/async/cpp/task.h>
#include <lib/async/cpp/time.h>
#include <lib/async/default.h>
#include <lib/fdio/directory.h>
#include <lib/fdio/fd.h>
#include <lib/fdio/fdio.h>
#include <lib/inspect_deprecated/inspect.h>
#include <lib/vfs/cpp/internal/file.h>
#include <lib/vfs/cpp/pseudo_file.h>
#include <lib/zx/time.h>
#include <lib/zx/vmo.h>
#include <string.h>
#include <zircon/status.h>
#include <zircon/types.h>

#include <iostream>

#include <src/lib/fxl/command_line.h>
#include <src/lib/fxl/logging.h>
#include <src/lib/fxl/strings/string_number_conversions.h>
#include <trace/event.h>

#include "src/developer/memory/metrics/capture.h"
#include "src/developer/memory/metrics/printer.h"
#include "src/developer/memory/monitor/high_water.h"

namespace monitor {

using namespace memory;

const char Monitor::kTraceName[] = "memory_monitor";

namespace {
const zx::duration kPollFrequency = zx::sec(10);
const uint64_t kThreshold = 10 * 1024 * 1024;
}  // namespace

Monitor::Monitor(std::unique_ptr<sys::ComponentContext> context,
                 const fxl::CommandLine& command_line, async_dispatcher_t* dispatcher)
    : high_water_(
          "/cache", kPollFrequency, kThreshold, dispatcher,
          [this](Capture& c, CaptureLevel l) { return Capture::GetCapture(c, capture_state_, l); }),
      prealloc_size_(0),
      logging_(command_line.HasOption("log")),
      tracing_(false),
      delay_(zx::sec(1)),
      dispatcher_(dispatcher),
      component_context_(std::move(context)) {
  auto s = Capture::GetCaptureState(capture_state_);
  if (s != ZX_OK) {
    FXL_LOG(ERROR) << "Error getting capture state: " << zx_status_get_string(s);
    exit(EXIT_FAILURE);
  }

  vfs::PseudoDir* dir = component_context_->outgoing()->GetOrCreateDirectory("objects");
  auto capture_file = std::make_unique<vfs::PseudoFile>(
      1024 * 1024, [this](std::vector<uint8_t>* output, size_t max_bytes) {
        return Inspect(output, max_bytes);
      });
  dir->AddEntry("root.inspect", std::move(capture_file));
  component_context_->outgoing()->AddPublicService(bindings_.GetHandler(this));

  if (command_line.HasOption("help")) {
    PrintHelp();
    exit(EXIT_SUCCESS);
  }
  std::string delay_as_string;
  if (command_line.GetOptionValue("delay", &delay_as_string)) {
    unsigned delay_as_int;
    if (!fxl::StringToNumberWithError<unsigned>(delay_as_string, &delay_as_int)) {
      FXL_LOG(ERROR) << "Invalid value for delay: " << delay_as_string;
      exit(-1);
    }
    delay_ = zx::msec(delay_as_int);
  }
  std::string prealloc_as_string;
  if (command_line.GetOptionValue("prealloc", &prealloc_as_string)) {
    FXL_LOG(INFO) << "prealloc_string: " << prealloc_as_string;
    if (!fxl::StringToNumberWithError<uint64_t>(prealloc_as_string, &prealloc_size_)) {
      FXL_LOG(ERROR) << "Invalid value for prealloc: " << prealloc_as_string;
      exit(-1);
    }
    prealloc_size_ *= (1024 * 1024);
    auto status = zx::vmo::create(prealloc_size_, 0, &prealloc_vmo_);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmo::create() returns " << zx_status_get_string(status);
      exit(-1);
    }
    prealloc_vmo_.get_size(&prealloc_size_);
    uintptr_t prealloc_addr = 0;
    status = zx::vmar::root_self()->map(0, prealloc_vmo_, 0, prealloc_size_, ZX_VM_PERM_READ,
                                        &prealloc_addr);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmar::map() returns " << zx_status_get_string(status);
      exit(-1);
    }

    status = prealloc_vmo_.op_range(ZX_VMO_OP_COMMIT, 0, prealloc_size_, NULL, 0);
    if (status != ZX_OK) {
      FXL_LOG(ERROR) << "zx::vmo::op_range() returns " << zx_status_get_string(status);
      exit(-1);
    }
  }

  trace_observer_.Start(dispatcher_, [this] { UpdateState(); });
  if (logging_) {
    Capture capture;
    auto s = Capture::GetCapture(capture, capture_state_, KMEM);
    if (s != ZX_OK) {
      FXL_LOG(ERROR) << "Error getting capture: " << zx_status_get_string(s);
      exit(EXIT_FAILURE);
    }
    const auto& kmem = capture.kmem();
    FXL_LOG(INFO) << "Total: " << kmem.total_bytes << " Wired: " << kmem.wired_bytes
                  << " Total Heap: " << kmem.total_heap_bytes;
  }
  SampleAndPost();
}

Monitor::~Monitor() {
  // TODO(CF-257).
  root_object_.set_children_callback(nullptr);
}

void Monitor::Watch(fidl::InterfaceHandle<fuchsia::memory::Watcher> watcher) {
  fuchsia::memory::WatcherPtr watcher_proxy = watcher.Bind();
  fuchsia::memory::Watcher* proxy_raw_ptr = watcher_proxy.get();
  watcher_proxy.set_error_handler(
      [this, proxy_raw_ptr](zx_status_t status) { ReleaseWatcher(proxy_raw_ptr); });
  watchers_.push_back(std::move(watcher_proxy));
  SampleAndPost();
}

void Monitor::ReleaseWatcher(fuchsia::memory::Watcher* watcher) {
  auto predicate = [watcher](const auto& target) { return target.get() == watcher; };
  watchers_.erase(std::remove_if(watchers_.begin(), watchers_.end(), predicate));
}

void Monitor::NotifyWatchers(const zx_info_kmem_stats_t& kmem_stats) {
  fuchsia::memory::Stats stats{
      .total_bytes = kmem_stats.total_bytes,
      .free_bytes = kmem_stats.free_bytes,
      .wired_bytes = kmem_stats.wired_bytes,
      .total_heap_bytes = kmem_stats.total_heap_bytes,
      .free_heap_bytes = kmem_stats.free_heap_bytes,
      .vmo_bytes = kmem_stats.vmo_bytes,
      .mmu_overhead_bytes = kmem_stats.mmu_overhead_bytes,
      .ipc_bytes = kmem_stats.ipc_bytes,
      .other_bytes = kmem_stats.other_bytes,
  };

  for (auto& watcher : watchers_) {
    watcher->OnChange(stats);
  }
}

void Monitor::PrintHelp() {
  std::cout << "memory_monitor [options]" << std::endl;
  std::cout << "Options:" << std::endl;
  std::cout << "  --log" << std::endl;
  std::cout << "  --prealloc=kbytes" << std::endl;
  std::cout << "  --delay=msecs" << std::endl;
}

zx_status_t Monitor::Inspect(std::vector<uint8_t>* output, size_t max_bytes) {
  inspect_deprecated::Inspector inspector;
  auto tree = inspector.CreateTree(
      "root", inspect_deprecated::TreeSettings{.initial_size = 4096, .maximum_size = 1024 * 1024});
  auto& root = tree.GetRoot();
  Capture c;
  Capture::GetCapture(c, capture_state_, VMO);
  Summary s(c, Summary::kNameMatches);
  std::ostringstream oss;
  Printer p(oss);
  p.PrintSummary(s, VMO, SORTED);

  auto current_string = oss.str();
  auto high_water_string = high_water_.GetHighWater();
  auto previous_high_water_string = high_water_.GetPreviousHighWater();
  inspect_deprecated::StringProperty current, high_water, previous_high_water;

  if (!current_string.empty()) {
    current = root.CreateStringProperty("current", current_string);
  }
  if (!high_water_string.empty()) {
    high_water = root.CreateStringProperty("high_water", high_water_string);
  }
  if (!previous_high_water_string.empty()) {
    previous_high_water =
        root.CreateStringProperty("high_water_previous_boot", previous_high_water_string);
  }

  size_t size;
  tree.GetVmo().get_size(&size);
  output->resize(size);
  return tree.GetVmo().read(output->data(), 0, size);
}

void Monitor::SampleAndPost() {
  if (logging_ || tracing_ || watchers_.size() > 0) {
    Capture capture;
    auto s = Capture::GetCapture(capture, capture_state_, KMEM);
    if (s != ZX_OK) {
      FXL_LOG(ERROR) << "Error getting capture: " << zx_status_get_string(s);
      return;
    }
    const auto& kmem = capture.kmem();
    if (logging_) {
      FXL_LOG(INFO) << "Free: " << kmem.free_bytes << " Free Heap: " << kmem.free_heap_bytes
                    << " VMO: " << kmem.vmo_bytes << " MMU: " << kmem.mmu_overhead_bytes
                    << " IPC: " << kmem.ipc_bytes;
    }
    if (tracing_) {
      TRACE_COUNTER(kTraceName, "allocated", 0, "vmo", kmem.vmo_bytes, "mmu_overhead",
                    kmem.mmu_overhead_bytes, "ipc", kmem.ipc_bytes);
      TRACE_COUNTER(kTraceName, "free", 0, "free", kmem.free_bytes, "free_heap",
                    kmem.free_heap_bytes);
    }
    NotifyWatchers(kmem);
    async::PostDelayedTask(
        dispatcher_, [this] { SampleAndPost(); }, delay_);
  }
}

void Monitor::UpdateState() {
  if (trace_state() == TRACE_STARTED) {
    if (trace_is_category_enabled(kTraceName)) {
      FXL_LOG(INFO) << "Tracing started";
      if (!tracing_) {
        Capture capture;
        auto s = Capture::GetCapture(capture, capture_state_, KMEM);
        if (s != ZX_OK) {
          FXL_LOG(ERROR) << "Error getting capture: " << zx_status_get_string(s);
          return;
        }
        const auto& kmem = capture.kmem();
        TRACE_COUNTER(kTraceName, "fixed", 0, "total", kmem.total_bytes, "wired", kmem.wired_bytes,
                      "total_heap", kmem.total_heap_bytes);
        tracing_ = true;
        if (!logging_) {
          SampleAndPost();
        }
      }
    }
  } else {
    if (tracing_) {
      FXL_LOG(INFO) << "Tracing stopped";
      tracing_ = false;
    }
  }
}

}  // namespace monitor
