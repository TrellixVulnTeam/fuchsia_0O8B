// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package target

import (
	"context"
	"encoding/json"
	"errors"
	"fmt"
	"io"
	"io/ioutil"
	"net"
	"time"

	"go.fuchsia.dev/fuchsia/tools/bootserver/lib"
	"go.fuchsia.dev/fuchsia/tools/botanist/lib"
	"go.fuchsia.dev/fuchsia/tools/botanist/power/lib"
	"go.fuchsia.dev/fuchsia/tools/build/lib"
	"go.fuchsia.dev/fuchsia/tools/lib/logger"
	"go.fuchsia.dev/fuchsia/tools/net/netboot"
	"go.fuchsia.dev/fuchsia/tools/net/netutil"
	"go.fuchsia.dev/fuchsia/tools/net/tftp"
	"go.fuchsia.dev/fuchsia/tools/serial"

	"golang.org/x/crypto/ssh"
)

const (
	// The duration we allow for the netstack to come up when booting.
	netstackTimeout = 90 * time.Second
)

// DeviceConfig contains the static properties of a target device.
type DeviceConfig struct {
	// Network is the network properties of the target.
	Network NetworkProperties `json:"network"`

	// Power is the attached power management configuration.
	Power *power.Client `json:"power,omitempty"`

	// SSHKeys are the default system keys to be used with the device.
	SSHKeys []string `json:"keys,omitempty"`

	// Serial is the path to the device file for serial i/o.
	Serial string `json:"serial,omitempty"`
}

// NetworkProperties are the static network properties of a target.
type NetworkProperties struct {
	// Nodename is the hostname of the device that we want to boot on.
	Nodename string `json:"nodename"`

	// IPv4Addr is the IPv4 address, if statically given. If not provided, it may be
	// resolved via the netstack's MDNS server.
	IPv4Addr string `json:"ipv4"`
}

// LoadDeviceConfigs unmarshalls a slice of device configs from a given file.
func LoadDeviceConfigs(path string) ([]DeviceConfig, error) {
	data, err := ioutil.ReadFile(path)
	if err != nil {
		return nil, fmt.Errorf("failed to read device properties file %q", path)
	}

	var configs []DeviceConfig
	if err := json.Unmarshal(data, &configs); err != nil {
		return nil, fmt.Errorf("failed to unmarshal configs: %v", err)
	}
	return configs, nil
}

// DeviceTarget represents a target device.
type DeviceTarget struct {
	config  DeviceConfig
	opts    Options
	signers []ssh.Signer
	serial  io.ReadWriteCloser
	tftp    tftp.Client
}

// NewDeviceTarget returns a new device target with a given configuration.
func NewDeviceTarget(ctx context.Context, config DeviceConfig, opts Options) (*DeviceTarget, error) {
	// If an SSH key is specified in the options, prepend it the configs list so that it
	// corresponds to the authorized key that would be paved.
	if opts.SSHKey != "" {
		config.SSHKeys = append([]string{opts.SSHKey}, config.SSHKeys...)
	}
	signers, err := parseOutSigners(config.SSHKeys)
	if err != nil {
		return nil, fmt.Errorf("could not parse out signers from private keys: %v", err)
	}
	var s io.ReadWriteCloser
	if config.Serial != "" {
		s, err = serial.Open(config.Serial)
		if err != nil {
			// TODO(IN-????): This should be returned as an error, but we don't want to fail any
			// test runs for misconfigured serial until it is actually required to complete certain
			// tasks.
			logger.Errorf(ctx, "unable to open %s: %v", config.Serial, err)
		}
	}
	addr, err := netutil.GetNodeAddress(ctx, config.Network.Nodename, false)
	if err != nil {
		return nil, err
	}
	t, err := tftp.NewClient(&net.UDPAddr{
		IP:   addr.IP,
		Port: tftp.ClientPort,
		Zone: addr.Zone,
	})
	if err != nil {
		return nil, err
	}
	return &DeviceTarget{
		config:  config,
		opts:    opts,
		signers: signers,
		serial:  s,
		tftp:    t,
	}, nil
}

// Tftp returns a tftp client interface for the device.
func (t *DeviceTarget) Tftp() tftp.Client {
	return t.tftp
}

// Nodename returns the name of the node.
func (t *DeviceTarget) Nodename() string {
	return t.config.Network.Nodename
}

// IPv4Addr returns the IPv4 address of the node. If not provided in the config, then it
// will be resolved against the target-side MDNS server.
func (t *DeviceTarget) IPv4Addr() (net.IP, error) {
	if t.config.Network.IPv4Addr != "" {
		return net.ParseIP(t.config.Network.IPv4Addr), nil
	}
	return botanist.ResolveIPv4(context.Background(), t.Nodename(), netstackTimeout)
}

// Serial returns the serial device associated with the target for serial i/o.
func (t *DeviceTarget) Serial() io.ReadWriteCloser {
	return t.serial
}

// SSHKey returns the private SSH key path associated with the authorized key to be paved.
func (t *DeviceTarget) SSHKey() string {
	return t.config.SSHKeys[0]
}

// Start starts the device target.
func (t *DeviceTarget) Start(ctx context.Context, images []build.Image, args []string) error {
	// Set up log listener and dump kernel output to stdout.
	l, err := netboot.NewLogListener(t.Nodename())
	if err != nil {
		return fmt.Errorf("cannot listen: %v", err)
	}
	go func() {
		defer l.Close()
		for {
			data, err := l.Listen()
			if err != nil {
				continue
			}
			fmt.Print(data)
			if ctx.Err() != nil {
				return
			}
		}
	}()

	// Get boot mode and ssh signers.
	// We cannot have signers in netboot because there is no notion
	// of a hardware backed key when you are not booting from disk
	var bootMode bootserver.Mode
	var signers []ssh.Signer
	if t.opts.Netboot {
		bootMode = bootserver.ModeNetboot
	} else {
		bootMode = bootserver.ModePave
		signers = t.signers
	}

	// Convert build images to bootserver images
	bootImgs := bootserver.ConvertFromBuildImages(images, bootMode)

	// TODO(fxbug.dev/38517): remove this once BootZedbootShim is deprecated
	paveImgs := bootserver.ConvertFromBuildImages(images, bootserver.ModePave)
	// Mexec Zedboot
	err = bootserver.BootZedbootShim(ctx, t.Tftp(), paveImgs)
	if err != nil {
		return err
	}

	// Boot Fuchsia.
	return bootserver.Boot(ctx, t.Tftp(), bootImgs, args, signers)
}

// Restart restarts the target.
func (t *DeviceTarget) Restart(ctx context.Context) error {
	if t.serial != nil {
		defer t.serial.Close()
	}
	if t.config.Power != nil {
		if err := t.config.Power.RebootDevice(t.signers, t.Nodename(), t.serial); err != nil {
			return fmt.Errorf("failed to reboot the device: %v", err)
		}
	}
	return nil
}

// Stop stops the device.
func (t *DeviceTarget) Stop(ctx context.Context) error {
	return ErrUnimplemented
}

// Wait waits for the device target to stop.
func (t *DeviceTarget) Wait(ctx context.Context) error {
	return ErrUnimplemented
}

func parseOutSigners(keyPaths []string) ([]ssh.Signer, error) {
	if len(keyPaths) == 0 {
		return nil, errors.New("must supply SSH keys in the config")
	}
	var keys [][]byte
	for _, keyPath := range keyPaths {
		p, err := ioutil.ReadFile(keyPath)
		if err != nil {
			return nil, fmt.Errorf("could not read SSH key file %q: %v", keyPath, err)
		}
		keys = append(keys, p)
	}

	var signers []ssh.Signer
	for _, p := range keys {
		signer, err := ssh.ParsePrivateKey(p)
		if err != nil {
			return nil, err
		}
		signers = append(signers, signer)
	}
	return signers, nil
}
