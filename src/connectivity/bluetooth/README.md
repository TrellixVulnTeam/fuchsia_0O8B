Bluetooth
=========

The Fuchsia Bluetooth system aims to provide a dual-mode implementation of the
Bluetooth Host Subsystem (5.0+) supporting a framework for developing Low Energy
and Traditional profiles.

Source code shortcuts:
- Public API:
  * [shared](/sdk/fidl/fuchsia.bluetooth)
  * [BR/EDR](/sdk/fidl/fuchsia.bluetooth.bredr)
  * [Control](/sdk/fidl/fuchsia.bluetooth.control)
  * [GATT](/sdk/fidl/fuchsia.bluetooth.gatt)
  * [LE](/sdk/fidl/fuchsia.bluetooth.le)
- [Private API](/src/connectivity/bluetooth/fidl)
- [Tools](tools/)
- [Host Bus Driver](core/bt-host)
- [HCI Drivers](hci)
- [HCI Transport Drivers](https://fuchsia.googlesource.com/fuchsia/+/master/zircon/system/dev/bluetooth?autodive=0)

For more orientation, see
- [System Architecture](/docs/the-book/bluetooth_architecture.md)
- [Detailed Source Layout](/docs/the-book/bluetooth_source_layout.md)
- [Respectful Code](#Respectful-Code)

For a note on used (and avoided) vocabulary, see
- [Bluetooth Vocabulary](docs/vocabulary.md)

## Getting Started

### API Examples

Examples using Fuchsia's Bluetooth Low Energy APIs can be found
[here](examples).

### Control API

Dual-mode (LE + Classic) GAP operations that are typically exposed to privileged
clients are performed using the [control.fidl](/sdk/fidl/fuchsia.bluetooth.control/control.fidl)
API. This API is intended for managing local adapters, device discovery & discoverability,
pairing/bonding, and other settings.

[`bt-cli`](tools/bt-cli) is a command-line front-end
for this API:

  ```
  $ bt-cli
  bluetooth> list-adapters
    Adapter 0
      id: bf004a8b-d691-4298-8c79-130b83e047a1
      address: 00:1A:7D:DA:0A
  bluetooth>
  ```

We also have a Flutter [module](/docs/glossary#module)
that acts as a Bluetooth system menu based on this API at
[topaz/bin/bluetooth\_settings](https://fuchsia.googlesource.com/topaz/+/master/bin/bluetooth_settings/).

### Tools

See the [bluetooth/tools](tools/) package for more information on
available command line tools for testing/debugging.

### Running Tests

Your build configuration may or may not include Bluetooth tests. Ensure
Bluetooth tests are built and installed when paving or OTA'ing with [`fx set`](docs/development/workflows/fx.md#configure-a-build):

  ```
  $ fx set workstation.x64 --with-base="//bundles:tools,//src/connectivity/bluetooth"
  ```

#### Tests

Bluetooth test packages are listed in
[tests/bluetooth](/garnet/packages/tests/bluetooth) and each contains at least one
test binary. Refer to package definitions for each package's binaries.

Each test binary is a [component](/docs/glossary.md#component)
whose runtime environment is defined by its [`.cmx` component manifest](/docs/the-book/package_metadata.md#Component-Manifest)

For example, `bt-host-unittests` is a [Google Test](https://github.com/google/googletest)
binary that contains all the C++ bt-host subsystem unit tests and is a part of
the [`bluetooth-tests`](tests/BUILD.gn) package.

##### Running on a Fuchsia device

* Run all the bt-host unit tests from the target shell:

  ```
  $ run-test-component bt-host-unittests
  ```

* Or use the `--gtest_filter`
[flag](https://github.com/google/googletest/blob/master/googletest/docs/advanced.md#running-a-subset-of-the-tests) to run a subset of the tests:

  ```
  # This only runs the L2CAP unit tests.
  $ run-test-component bt-host-unittests --gtest_filter=L2CAP_\*
  ```

* And use the `--verbose` flag to set log verbosity:

  ```
  # This logs all messages logged using FXL_VLOG up to level 2 (equivalent to ::bt::common::LogSeverity:SPEW)
  $ run-test-component bt-host-unittests --verbose=2
  ```

* After making library or test changes, you can push the test package and run it
from your development shell:

  ```
  $ fx run-test bluetooth-tests -t bt-host-unittests -- --gtest_filter=L2CAP_\*
  ```

Note the use of the package name `bluetooth-tests` and the extra `--` used to
separate arguments passed to the test binary.

See [Developing with Fuchsia packages](/docs/development/workflows/package_update.md)
for more details on the package-based workflow.

##### Running on QEMU

If you don't have physical hardware available, you can run the tests in QEMU using the same commands as above. A couple of tips will help run the tests a little more quickly.

* Run the VM with hardware virtualization support: `fx emu`
* Disable unnecessary logging for the tests:

  ```
  $ run-test-component bt-host-unittests --quiet=10
  ```

With these two tips, the full bt-host-unittests suite runs in ~2 seconds.

#### Integration Tests

See the [Integration Test README](tests/integration/README.md)

### Controlling Log Verbosity

#### Drivers

The most reliable way to enable higher log verbosity is with kernel command line parameters. These can be configured through the `fx set` command:

  ```
  fx set workstation.x64 --args="kernel_cmdline_files=[\"//local/kernel_cmdline.txt\"]"
  ```

Add the commands to `$FUCHSIA_DIR/local/kernel_cmdline.txt`, e.g. to enable full logging for the USB transport, Intel HCI, and host drivers:

  ```
  $ cat $FUCHSIA_DIR/local/kernel_cmdline.txt
  driver.bt_host.log=+trace,+spew,+info,+error,+warn
  driver.bt_hci_intel.log=+trace,+spew,+info,+error,+warn
  driver.bt_transport_usb.log=+trace,+info,+error,+warn
  ```

(HCI drivers other than Intel can also be set. Other hci drivers include `bt_hci_atheros`, `bt_hci_passthrough`, and `bt_hci_emulator`)

Using `fx set` writes these values into the image, so they will survive a restart.

For more detail on driver logging, see [Zircon driver logging](/docs/concepts/drivers/driver-development#logging)

#### bin/bt-gap

The Bluetooth system service is invoked by sysmgr to resolve service requests.
The mapping between environment service names and their handlers is defined in
[//src/sys/sysmgr/config/services.config](/src/sys/sysmgr/config/services.config).
Add the `--verbose` option to the Bluetooth entries to increase verbosity, for
example:

  ```
  ...
    "fuchsia.bluetooth.bredr.Profile":  "fuchsia-pkg://fuchsia.com/bt-init#meta/bt-init.cmx",
    "fuchsia.bluetooth.control.Control": "fuchsia-pkg://fuchsia.com/bt-init#meta/bt-init.cmx",
    "fuchsia.bluetooth.gatt.Server":  "fuchsia-pkg://fuchsia.com/bt-init#meta/bt-init.cmx",
    "fuchsia.bluetooth.le.Central":  "fuchsia-pkg://fuchsia.com/bt-init#meta/bt-init.cmx",
    "fuchsia.bluetooth.le.Peripheral":  "fuchsia-pkg://fuchsia.com/bt-init#meta/bt-init.cmx",
    "fuchsia.bluetooth.snoop.Snoop":  "fuchsia-pkg://fuchsia.com/bt-snoop#meta/bt-snoop.cmx",
  ...

  ```

### Inspecting Component State

The Bluetooth system supports inspection through the [Inspect API](/docs/development/inspect).
bt-gap, bt-a2dp-sink, and bt-snoop all expose information though Inspect.

#### Usage

* bt-gap: `fx iquery bt-gap` exposes information on host devices managed by bt-gap, pairing capabilities, stored bonds, and actively connected peers.
* bt-a2dp-sink: `fx iquery bt-a2dp-sink` exposes information on audio streaming capabilities and active streams
* bt-snoop: `fx iquery bt-snoop` exposes information about which hci devices are being logged and how much data is stored.
* All Bluetooth components: `fx iquery bt-*`

See the [iquery documentation](/docs/development/inspect/iquery) for complete instructions on using `iquery`.

### Respectful Code

Inclusivity is central to Fuchsia's culture, and our values include treating
each other with dignity. As such, it’s important that everyone can contribute
without facing the harmful effects of bias and discrimination.

The Bluetooth standard makes use of the terms "master" and "slave" to define
link layer connection roles in many of the protocol specifications. Here are a
few rules of thumb when referring to these roles in code and comments:

1. Do not propagate these terms beyond the layer of code directly involved with link layer
roles. Use the suggested alternative terminology at FIDL API boundaries. See
[Bluetooth Vocabulary Guide](//src/connectivity/bluetooth/docs/vocabulary.md).

2. Whenever possible, prefer different terms that more specifically describe function. For example,
the SMP specification defines "initiator" and "responder" roles that correspond to the
aforementioned roles without loss of clarity.

3. If an explicit reference to the link layer role is necessary, then try to
avoid the term "slave" where possible. For example this formulation avoids the
term without losing clarity:

```
   if (link->role() != hci::Connection::Role::kMaster) {
     ...
   }
```

See the Fuchsia project [guide](//docs/best-practices/respectful_code.md) on best practices
for more information.
