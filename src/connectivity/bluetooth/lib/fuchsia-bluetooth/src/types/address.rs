// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{bail, Error},
    fidl_fuchsia_bluetooth as fidl,
    std::fmt,
};

/// A Bluetooth device address can either be public or private. The controller device address used
/// in BR/EDR (aka BD_ADDR) and LE have the "public" address type. A private address is one that is
/// randomly generated by the controller or the host and can only be used in LE. The identity
/// address can be random (often "static random") but is not typically considered private.
///
/// Some controller procedures depend on knowledge of whether an address is public (i.e. the BD_ADDR
/// assigned to the controller) or randomly assigned by the host. This enum type represents that
/// information.
///
/// This type represents a Bluetooth device address. `Address` can be converted to/from a FIDL
/// Bluetooth device address type.
#[derive(Copy, Clone, Debug, Eq, Hash, PartialEq)]
pub enum Address {
    Public(AddressBytes),
    Random(AddressBytes),
}

const NUM_ADDRESS_BYTES: usize = 6;
type AddressBytes = [u8; NUM_ADDRESS_BYTES];

fn addr_to_string(bytes: &AddressBytes) -> String {
    format!(
        "{:02X}:{:02X}:{:02X}:{:02X}:{:02X}:{:02X}",
        bytes[5], bytes[4], bytes[3], bytes[2], bytes[1], bytes[0]
    )
}

impl Address {
    /// Returns a string representation of the address bytes.
    // TODO(armansito): This method is temporarily used for the deprecated FIDL APIs that represent
    // addresses as strings. Remove this method once all of those APIs are removed.
    pub fn to_string(&self) -> String {
        match self {
            Address::Public(b) => addr_to_string(b),
            Address::Random(b) => addr_to_string(b),
        }
    }

    pub fn to_fidl(&self) -> fidl::Address {
        match self {
            Address::Public(b) => {
                fidl::Address { type_: fidl::AddressType::Public, bytes: b.clone() }
            }
            Address::Random(b) => {
                fidl::Address { type_: fidl::AddressType::Random, bytes: b.clone() }
            }
        }
    }
    pub fn public_from_str(s: &str) -> Result<Address, Error> {
        Ok(Address::Public(le_bytes_from_be_str(s)?))
    }
    pub fn random_from_str(s: &str) -> Result<Address, Error> {
        Ok(Address::Random(le_bytes_from_be_str(s)?))
    }

    pub fn address_type_string(&self) -> String {
        match self {
            Address::Public(_) => "Public".to_string(),
            Address::Random(_) => "Random".to_string(),
        }
    }
}

/// Read a string of bytes in BigEndian Hex and produce a slice in LittleEndian
/// E.g. 0x010203040506 becomes [6,5,4,3,2,1]
fn le_bytes_from_be_str(s: &str) -> Result<AddressBytes, Error> {
    let mut bytes = [0; 6];
    let mut insert_cursor = 6; // Start back and work forward

    for octet in s.split(|c| c == ':') {
        if insert_cursor == 0 {
            bail!("Too many octets");
        }
        bytes[insert_cursor - 1] = u8::from_str_radix(octet, 16)?;
        insert_cursor -= 1;
    }

    if insert_cursor != 0 {
        bail!("Too few octets")
    }
    Ok(bytes)
}

impl From<&fidl::Address> for Address {
    fn from(src: &fidl::Address) -> Address {
        match src.type_ {
            fidl::AddressType::Public => Address::Public(src.bytes.clone()),
            fidl::AddressType::Random => Address::Random(src.bytes.clone()),
        }
    }
}

impl From<fidl::Address> for Address {
    fn from(src: fidl::Address) -> Address {
        Address::from(&src)
    }
}

impl Into<fidl::Address> for Address {
    fn into(self) -> fidl::Address {
        self.to_fidl()
    }
}

impl Into<fidl::Address> for &Address {
    fn into(self) -> fidl::Address {
        self.to_fidl()
    }
}

impl fmt::Display for Address {
    fn fmt(&self, fmt: &mut fmt::Formatter<'_>) -> fmt::Result {
        match self {
            Address::Public(b) => write!(fmt, "[address (public) {}]", self.to_string()),
            Address::Random(b) => write!(fmt, "[address (random) {}]", self.to_string()),
        }
    }
}

#[cfg(test)]
pub mod tests {
    use super::*;
    use proptest::prelude::*;

    pub fn any_public_address() -> impl Strategy<Value = Address> {
        any::<[u8; 6]>().prop_map(Address::Public)
    }

    pub fn any_random_address() -> impl Strategy<Value = Address> {
        any::<[u8; 6]>().prop_map(Address::Random)
    }

    pub fn any_address() -> impl Strategy<Value = Address> {
        prop_oneof![any_public_address(), any_random_address()]
    }

    proptest! {
        #[test]
        fn public_address_str_roundtrip(address in any_public_address()) {
            let str_rep = address.to_string();
            assert_eq!(
                Address::public_from_str(&str_rep).map_err(|e| e.to_string()),
                Ok(address),
            );
        }

        #[test]
        fn random_address_str_roundtrip(address in any_random_address()) {
            let str_rep = address.to_string();
            assert_eq!(
                Address::random_from_str(&str_rep).map_err(|e| e.to_string()),
                Ok(address),
            );
        }

        #[test]
        fn any_address_fidl_roundtrip(address in any_address()) {
            let fidl_address: fidl::Address = address.into();
            assert_eq!(address, fidl_address.into());
        }
    }

    #[test]
    fn address_to_string() {
        let address = Address::Public([0x01, 0x02, 0x03, 0xDD, 0xEE, 0xFF]);
        assert_eq!("FF:EE:DD:03:02:01", address.to_string());
    }

    #[test]
    fn address_from_string_too_few_octets() {
        let str_rep = "01:02:03:04:05";
        let parsed = Address::public_from_str(&str_rep).map_err(|e| e.to_string());
        assert_eq!(parsed, Err("Too few octets".to_string()));
    }
    #[test]
    fn address_from_string_too_many_octets() {
        let str_rep = "01:02:03:04:05:06:07";
        let parsed = Address::public_from_str(&str_rep).map_err(|e| e.to_string());
        assert_eq!(parsed, Err("Too many octets".to_string()));
    }
    #[test]
    fn address_from_string_non_hex_chars() {
        let str_rep = "01:02:03:04:05:0G";
        let parsed = Address::public_from_str(&str_rep).map_err(|e| e.to_string());
        assert_eq!(parsed, Err("invalid digit found in string".to_string()));
    }

    #[test]
    fn public_address_from_fidl() {
        let fidl_address =
            fidl::Address { type_: fidl::AddressType::Public, bytes: [1, 2, 3, 4, 5, 6] };
        let address: Address = fidl_address.into();
        assert_eq!(Address::Public([1, 2, 3, 4, 5, 6]), address);
    }

    #[test]
    fn random_address_from_fidl() {
        let fidl_address =
            fidl::Address { type_: fidl::AddressType::Random, bytes: [1, 2, 3, 4, 5, 6] };
        let address: Address = fidl_address.into();
        assert_eq!(Address::Random([1, 2, 3, 4, 5, 6]), address);
    }

    #[test]
    fn public_address_into_fidl() {
        let address = Address::Public([1, 2, 3, 4, 5, 6]);
        let fidl_address: fidl::Address = address.into();
        assert_eq!(fidl::AddressType::Public, fidl_address.type_);
        assert_eq!([1, 2, 3, 4, 5, 6], fidl_address.bytes);
    }

    #[test]
    fn random_address_into_fidl() {
        let address = Address::Random([1, 2, 3, 4, 5, 6]);
        let fidl_address: fidl::Address = address.into();
        assert_eq!(fidl::AddressType::Random, fidl_address.type_);
        assert_eq!([1, 2, 3, 4, 5, 6], fidl_address.bytes);
    }
}
