// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#ifndef SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_L2CAP_BASIC_MODE_TX_ENGINE_H_
#define SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_L2CAP_BASIC_MODE_TX_ENGINE_H_

#include "src/connectivity/bluetooth/core/bt-host/l2cap/tx_engine.h"

namespace bt {
namespace l2cap {
namespace internal {

// Implements the sender-side functionality of L2CAP Basic Mode. See Bluetooth
// Core Spec v5.0, Volume 3, Part A, Sec 2.4, "Modes of Operation".
//
// THREAD-SAFETY: This class may is _not_ thread-safe. In particular, the class
// assumes that some other party ensures that QueueSdu() is not invoked
// concurrently with the destructor.
class BasicModeTxEngine final : public TxEngine {
 public:
  BasicModeTxEngine(ChannelId channel_id, uint16_t tx_mtu, SendFrameCallback send_frame_callback)
      : TxEngine(channel_id, tx_mtu, std::move(send_frame_callback)) {}
  ~BasicModeTxEngine() override = default;

  // Queues |sdu| for transmission, returning true on success. This may fail,
  // e.g., if the |sdu| is larger than |tx_mtu_|.
  bool QueueSdu(ByteBufferPtr sdu) override;

 private:
  DISALLOW_COPY_AND_ASSIGN_ALLOW_MOVE(BasicModeTxEngine);
};

}  // namespace internal
}  // namespace l2cap
}  // namespace bt

#endif  // SRC_CONNECTIVITY_BLUETOOTH_CORE_BT_HOST_L2CAP_BASIC_MODE_TX_ENGINE_H_
