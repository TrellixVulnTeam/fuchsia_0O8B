// Copyright 2017 The Fuchsia Authors
//
// Use of this source code is governed by a MIT-style
// license that can be found in the LICENSE file or at
// https://opensource.org/licenses/MIT

#ifndef ZIRCON_KERNEL_SYSCALLS_SYSTEM_PRIV_H_
#define ZIRCON_KERNEL_SYSCALLS_SYSTEM_PRIV_H_

#include <zircon/syscalls/system.h>

zx_status_t arch_system_powerctl(uint32_t cmd, const zx_system_powerctl_arg_t* arg);

#endif  // ZIRCON_KERNEL_SYSCALLS_SYSTEM_PRIV_H_
