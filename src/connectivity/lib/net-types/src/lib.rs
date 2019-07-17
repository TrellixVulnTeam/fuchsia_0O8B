// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Networking types and operations.
//!
//! This crate defines types and operations useful for operating with various
//! network protocols. Some general utilities are defined in the crate root,
//! while protocol-specific operations are defined in their own modules.

#![cfg_attr(not(std), no_std)]

#[cfg(std)]
extern crate core;

pub mod ethernet;
pub mod ip;

use core::fmt::{self, Display, Formatter};
use core::ops::Deref;

mod sealed {
    // Used to ensure that certain traits cannot be implemented by anyone
    // outside this crate, such as the Ip and IpAddress traits.
    pub trait Sealed {}
}

// NOTE: The "witness" types UnicastAddr and MulticastAddr - which provide the
// invariant that the value they contain is a unicast or multicast address
// respectively - cannot actually guarantee this property without certain
// promises from the implementations of the UnicastAddress and MulticastAddress
// traits that they rely on. In particular, the values must be "immutable" in
// the sense that, given only immutable references to the values, nothing about
// the values can change such that the "unicast-ness" or "multicast-ness" of the
// values change. Since the UnicastAddress and MulticastAddress traits are not
// unsafe traits, it would be unsound for unsafe code to rely for its soundness
// on this behavior. For a more in-depth discussion of why this isn't possible
// without an explicit opt-in on the part of the trait implementor, see this
// forum thread: https://users.rust-lang.org/t/prevent-interior-mutability/29403

/// Addresses that can be unicast.
///
/// `UnicastAddress` is implemented by address types for which some values are
/// considered [unicast] addresses. Unicast addresses are used to identify a
/// single network node, as opposed to broadcast and multicast addresses, which
/// identify a group of nodes.
///
/// `UnicastAddress` is only implemented for addresses whose unicast-ness can be
/// determined by looking only at the address itself (this is notably not true
/// for IPv4 addresses, which can be considered broadcast addresses depending on
/// the subnet in which they are used).
///
/// [unicast]: https://en.wikipedia.org/wiki/Unicast
pub trait UnicastAddress {
    /// Is this a unicast address?
    ///
    /// `is_unicast` must maintain the invariant that, if it is called twice on
    /// the same object, and in between those two calls, no code has operated on
    /// a mutable reference to that object, both calls will return the same
    /// value. This property is required in order to implement [`UnicastAddr`].
    /// Note that, since this is not an `unsafe` trait, `unsafe` code may NOT
    /// rely on this property for its soundness. However, code MAY rely on this
    /// property for its correctness.
    fn is_unicast(&self) -> bool;
}

/// Addresses that can be multicast.
///
/// `MulticastAddress` is implemented by address types for which some values are
/// considered [multicast] addresses. Multicast addresses are used to identify a
/// group of multiple network nodes, as opposed to unicast addresses, which
/// identify a single node, or broadcast addresses, which identify all the nodes
/// in some region of a network.
///
/// [multicast]: https://en.wikipedia.org/wiki/Multicast
pub trait MulticastAddress {
    /// Is this a unicast address?
    ///
    /// `is_multicast` must maintain the invariant that, if it is called twice
    /// on the same object, and in between those two calls, no code has operated
    /// on a mutable reference to that object, both calls will return the same
    /// value. This property is required in order to implement
    /// [`MulticastAddr`]. Note that, since this is not an `unsafe` trait,
    /// `unsafe` code may NOT rely on this property for its soundness. However,
    /// code MAY rely on this property for its correctness.
    fn is_multicast(&self) -> bool;
}

/// Addresses that can be broadcast.
///
/// `BroadcastAddress` is implemented by address types for which some values are
/// considered [broadcast] addresses. Broadcast addresses are used to identify
/// all the nodes in some region of a network, as opposed to unicast addresses,
/// which identify a single node, or multicast addresses, which identify a group
/// of nodes (not necessarily all of them).
///
/// [broadcast]: https://en.wikipedia.org/wiki/Broadcasting_(networking)
pub trait BroadcastAddress {
    /// Is this a broadcast address?
    fn is_broadcast(&self) -> bool;
}

/// An address which is guaranteed to be a unicast address.
///
/// `UnicastAddr` wraps an address of type `A` and guarantees that it is a
/// unicast address. Note that this guarantee is contingent on a correct
/// implementation of the [`UnicastAddress`]. Since that trait is not `unsafe`,
/// `unsafe` code may NOT rely on this guarantee for its soundness.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct UnicastAddr<A: UnicastAddress>(A);

impl<A: UnicastAddress> UnicastAddr<A> {
    /// Constructs a new `UnicastAddr`.
    ///
    /// `new` returns `None` if `addr` is not a unicast address according to
    /// [`UnicastAddress::is_unicast`].
    #[inline]
    pub fn new(addr: A) -> Option<UnicastAddr<A>> {
        if !addr.is_unicast() {
            return None;
        }
        Some(UnicastAddr(addr))
    }
}

impl<A: UnicastAddress + Clone> UnicastAddr<A> {
    /// Get a clone of the address.
    #[inline]
    pub fn get(&self) -> A {
        self.0.clone()
    }
}

impl<A: UnicastAddress> Deref for UnicastAddr<A> {
    type Target = A;

    #[inline]
    fn deref(&self) -> &A {
        &self.0
    }
}

impl<A: UnicastAddress + Display> Display for UnicastAddr<A> {
    #[inline]
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        self.0.fmt(f)
    }
}

/// An address which is guaranteed to be a multicast address.
///
/// `MulticastAddr` wraps an address of type `A` and guarantees that it is a
/// multicast address. Note that this guarantee is contingent on a correct
/// implementation of the [`MulticastAddress`]. Since that trait is not
/// `unsafe`, `unsafe` code may NOT rely on this guarantee for its soundness.
#[derive(Copy, Clone, Debug, Eq, PartialEq, Hash)]
pub struct MulticastAddr<A: MulticastAddress>(A);

impl<A: MulticastAddress> MulticastAddr<A> {
    /// Constructs a new `MulticastAddr`.
    ///
    /// `new` returns `None` if `addr` is not a multicast address according to
    /// [`MulticastAddress::is_multicast`].
    #[inline]
    pub fn new(addr: A) -> Option<MulticastAddr<A>> {
        if !addr.is_multicast() {
            return None;
        }
        Some(MulticastAddr(addr))
    }
}

impl<A: MulticastAddress + Clone> MulticastAddr<A> {
    /// Get a clone of the address.
    #[inline]
    pub fn get(&self) -> A {
        self.0.clone()
    }
}

impl<A: MulticastAddress> Deref for MulticastAddr<A> {
    type Target = A;

    #[inline]
    fn deref(&self) -> &A {
        &self.0
    }
}

impl<A: MulticastAddress + Display> Display for MulticastAddr<A> {
    #[inline]
    fn fmt(&self, f: &mut Formatter) -> fmt::Result {
        self.0.fmt(f)
    }
}

#[cfg(test)]
mod tests {
    use super::*;

    #[derive(Copy, Clone, Debug, Eq, PartialEq)]
    enum Address {
        Unicast,
        Multicast,
    }

    impl UnicastAddress for Address {
        fn is_unicast(&self) -> bool {
            *self == Address::Unicast
        }
    }

    impl MulticastAddress for Address {
        fn is_multicast(&self) -> bool {
            *self == Address::Multicast
        }
    }

    #[test]
    fn test_unicast_addr() {
        assert_eq!(UnicastAddr::new(Address::Unicast), Some(UnicastAddr(Address::Unicast)));
        assert_eq!(UnicastAddr::new(Address::Multicast), None);
    }

    #[test]
    fn test_multicast_addr() {
        assert_eq!(MulticastAddr::new(Address::Multicast), Some(MulticastAddr(Address::Multicast)));
        assert_eq!(MulticastAddr::new(Address::Unicast), None);
    }
}
