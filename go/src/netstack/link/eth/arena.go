// Copyright 2017 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

package eth

import (
	"fmt"
	"reflect"
	"sync"
	"syscall/zx"
	"unsafe"

	"fidl/fuchsia/hardware/ethernet"
)

// numBuffers is size of freebufs in arena
// numBuffers should be greater or equal to numOfInterfaces * fifoSizeOfInterface
// TODO (chunyingw) Design a mechanism to auto satisfy the above condtion
// wihtout manual manipulation of numBuffers value
const numBuffers = 2048
const bufferSize = 2048
const ioSize = numBuffers * bufferSize

// A Buffer is a single packet-sized segment of non-heap memory.
//
// The memory is part of a VMO and is shared with a separate server process.
//
// A Buffer must be acquired from an Arena.
// A Buffer must not by appended to beyond its initial capacity.
// A Buffer may be sliced.
type Buffer []byte

// An Arena is a block of non-heap memory allocated in a VMO.
// Arenas are split into fixed-size Buffers, and are shared with
// ethernet drivers.
type Arena struct {
	iovmo zx.VMO
	iobuf []byte

	mu struct {
		sync.Mutex
		freebufs []int
		owner    [numBuffers]*Client
	}
}

// NewArena creates a new Arena of fixed size.
func NewArena() (*Arena, error) {
	iovmo, err := zx.NewVMO(ioSize, zx.VMOOptionNonResizable)
	if err != nil {
		return nil, fmt.Errorf("eth: cannot allocate I/O VMO: %v", err)
	}
	iovmo.Handle().SetProperty(zx.PropName, []byte("eth-arena"))

	data, err := zx.VMARRoot.Map(0, iovmo, 0, ioSize, zx.VMFlagPermRead|zx.VMFlagPermWrite)
	if err != nil {
		iovmo.Close()
		return nil, fmt.Errorf("eth.Arena: I/O map failed: %v", err)
	}

	a := &Arena{
		iovmo: iovmo,
	}
	a.mu.freebufs = make([]int, numBuffers)
	for i := range a.mu.freebufs {
		a.mu.freebufs[i] = i
	}
	*(*reflect.SliceHeader)(unsafe.Pointer(&a.iobuf)) = reflect.SliceHeader{
		Data: data,
		Len:  0,
		Cap:  ioSize,
	}
	return a, nil
}

func (a *Arena) buffer(i int) Buffer {
	offset := i * bufferSize
	return a.iobuf[offset : offset+bufferSize]
}

func (a *Arena) allocIndex(c *Client) (int, bool) {
	a.mu.Lock()
	defer a.mu.Unlock()

	if len(a.mu.freebufs) == 0 {
		return 0, false
	}
	i := a.mu.freebufs[len(a.mu.freebufs)-1]
	a.mu.freebufs = a.mu.freebufs[:len(a.mu.freebufs)-1]
	if a.mu.owner[i] != nil {
		panic(fmt.Sprintf("eth.Arena: free list buffer %d is not free", i))
	}
	a.mu.owner[i] = c
	return i, true
}

func (a *Arena) alloc(c *Client) Buffer {
	if i, ok := a.allocIndex(c); ok {
		return a.buffer(i)
	}
	return nil
}

func (a *Arena) index(b Buffer) int {
	bp := (*reflect.SliceHeader)(unsafe.Pointer(&b)).Data
	ap := (*reflect.SliceHeader)(unsafe.Pointer(&a.iobuf)).Data
	i := (bp - ap) / bufferSize
	if i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer 0x%x (len=%d, cap=%d) not in iobuf 0x%x", bp, len(b), cap(b), ap))
	}
	return int(i)
}

func (a *Arena) free(c *Client, b Buffer) {
	i := a.index(b)

	a.mu.Lock()
	defer a.mu.Unlock()
	if a.mu.owner[i] != c {
		panic(fmt.Sprintf("eth.Arena: freeing a buffer owned by another client: %d (owner: %p, caller: %p)", i, a.mu.owner[i], c))
	}
	a.mu.owner[i] = nil
	a.mu.freebufs = append(a.mu.freebufs, i)
}

func (a *Arena) freeAll(c *Client) {
	a.mu.Lock()
	defer a.mu.Unlock()
	for i, owner := range a.mu.owner {
		if owner == c {
			a.mu.owner[i] = nil
			a.mu.freebufs = append(a.mu.freebufs, i)
		}
	}
}

func (a *Arena) entry(b Buffer) ethernet.FifoEntry {
	i := a.index(b)

	entry := ethernet.FifoEntry{
		Offset: uint32(i) * bufferSize,
		Length: uint16(len(b)),
		Cookie: (cookieMagic << 32) | uint64(i),
	}
	return entry
}

func (a *Arena) bufferFromEntry(e ethernet.FifoEntry) Buffer {
	i := int32(e.Cookie)
	if e.Cookie>>32 != cookieMagic || i < 0 || i >= numBuffers {
		panic(fmt.Sprintf("eth.Arena: buffer entry has bad cookie: %x", e.Cookie))
	}
	a.mu.Lock()
	isFree := a.mu.owner[i] == nil
	a.mu.Unlock()
	if isFree {
		panic(fmt.Sprintf("eth: buffer entry %d is on free list", i))
	}
	return a.buffer(int(i))[:e.Length]
}

const cookieMagic = 0x42420102 // used to fill top 32-bits of ethernet.FifoEntry.Cookie
