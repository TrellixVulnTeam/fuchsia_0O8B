// Copyright 2016 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

//! Error (common to all fidl operations)

use {
    failure::Fail,
    fuchsia_zircon_status as zx_status,
    std::{io, result},
};

/// A specialized `Result` type for FIDL operations.
pub type Result<T> = result::Result<T, Error>;

/// The error type used by FIDL operations.
#[derive(Fail, Debug)]
pub enum Error {
    /// Unexpected response to synchronous FIDL query.
    ///
    /// This will occur if an event or a response with an unexpected transaction
    /// ID is received when using the synchronous FIDL bindings.
    #[fail(display = "Unexpected response to synchronous FIDL query.")]
    UnexpectedSyncResponse,

    /// Invalid header for a FIDL buffer.
    #[fail(display = "Invalid header for a FIDL buffer.")]
    InvalidHeader,

    /// Unsupported wire format.
    #[fail(display = "Incompatible wire format magic number: {}.", _0)]
    IncompatibleMagicNumber(u8),

    /// Invalid FIDL buffer.
    #[fail(display = "Invalid FIDL buffer.")]
    Invalid,

    /// The FIDL object could not fit within the provided buffer range.
    #[fail(display = "The FIDL object could not fit within the provided buffer range")]
    OutOfRange,

    /// Decoding the FIDL object did not use all of the bytes provided.
    #[fail(display = "Decoding the FIDL object did not use all of the bytes provided.")]
    ExtraBytes,

    /// Decoding the FIDL object did not use all of the handles provided.
    #[fail(display = "Decoding the FIDL object did not use all of the handles provided.")]
    ExtraHandles,

    /// Decoding the FIDL object observed non-zero value in a padding byte.
    #[fail(
        display = "Decoding the FIDL object observed non-zero value in the padding at byte {}. \
                   Padding starts at byte {}.",
        non_zero_pos, padding_start
    )]
    NonZeroPadding {
        /// Index of the first byte of the padding, relative to the beginning of the message.
        padding_start: usize,
        /// Index of the byte in the padding that was non-zero, relative to the beginning of the
        /// message.
        non_zero_pos: usize,
    },

    /// The FIDL object had too many layers of structural recursion.
    #[fail(display = "The FIDL object had too many layers of structural recursion.")]
    MaxRecursionDepth,

    /// There was an attempt read or write a null-valued object as a non-nullable type.
    #[fail(
        display = "There was an attempt to read or write a null-valued object as a non-nullable FIDL type."
    )]
    NotNullable,

    /// A FIDL object reference with nonzero byte length had a null data pointer.
    #[fail(display = "A FIDL object reference with nonzero byte length had a null data pointer.")]
    UnexpectedNullRef,

    /// Incorrectly encoded UTF8.
    #[fail(display = "A FIDL message contained incorrectly encoded UTF8.")]
    Utf8Error,

    /// A message was recieved for an ordinal value that the service does not understand.
    /// This generally results from an attempt to call a FIDL service of a type other than
    /// the one being served.
    #[fail(
        display = "A message was received for ordinal value {} \
                   that the FIDL service {} does not understand.",
        ordinal, service_name
    )]
    UnknownOrdinal {
        /// The unknown ordinal.
        ordinal: u64,
        /// The name of the service for which the message was intented.
        service_name: &'static str,
    },

    /// Unrecognized descriminant for a FIDL union type.
    #[fail(display = "Unrecognized descriminant for a FIDL union type.")]
    UnknownUnionTag,

    /// Unrecognized field in a FIDL table type.
    #[fail(display = "Unrecognized field in a FIDL table type.")]
    UnknownTableField,

    /// A future was polled after it had already completed.
    #[fail(display = "A FIDL future was polled after it had already completed.")]
    PollAfterCompletion,

    /// A response message was received with txid 0.
    #[fail(display = "Invalid response with txid 0.")]
    InvalidResponseTxid,

    /// A handle which is invalid in the context of a host build of Fuchsia.
    #[fail(display = "Invalid FIDL handle used on the host.")]
    InvalidHostHandle,

    /// A FIDL server encountered an IO error writing a response to a channel.
    #[fail(
        display = "A server encountered an IO error writing a FIDL response to a channel: {}",
        _0
    )]
    ServerResponseWrite(#[cause] zx_status::Status),

    /// A FIDL server encountered an IO error reading incoming requests from a channel.
    #[fail(
        display = "A FIDL server encountered an IO error reading incoming FIDL requests from a channel: {}",
        _0
    )]
    ServerRequestRead(#[cause] zx_status::Status),

    /// A FIDL server encountered an IO error writing an epitaph to a channel.
    #[fail(
        display = "A FIDL server encountered an IO error writing an epitaph into a channel: {}",
        _0
    )]
    ServerEpitaphWrite(#[cause] zx_status::Status),

    /// A FIDL client encountered an IO error reading a response from a channel.
    #[fail(
        display = "A FIDL client encountered an IO error reading a response from a channel: {}",
        _0
    )]
    ClientRead(#[cause] zx_status::Status),

    /// A FIDL client encountered an IO error writing a request to a channel.
    #[fail(
        display = "A FIDL client encountered an IO error writing a request into a channel: {}",
        _0
    )]
    ClientWrite(#[cause] zx_status::Status),

    /// A FIDL client's channel was closed. Contains an epitaph if one was sent by the server, or
    /// `zx_status::Status::PEER_CLOSED` otherwise.
    #[fail(display = "A FIDL client's channel was closed: {}", _0)]
    ClientChannelClosed(#[cause] zx_status::Status),

    /// There was an error creating a channel to be used for a FIDL client-server pair.
    #[fail(
        display = "There was an error creating a channel to be used for a FIDL client-server pair: {}",
        _0
    )]
    ChannelPairCreate(#[cause] zx_status::Status),

    /// There was an error attaching a FIDL channel to the Tokio reactor.
    #[fail(display = "There was an error attaching a FIDL channel to the Tokio reactor: {}", _0)]
    AsyncChannel(#[cause] io::Error),

    /// There was a miscellaneous io::Error during a test.
    #[cfg(target_os = "fuchsia")]
    #[cfg(test)]
    #[fail(display = "Test zx_status::Status: {}", _0)]
    TestIo(#[cause] zx_status::Status),

    #[doc(hidden)]
    #[fail(display = "__Nonexhaustive error should never be created.")]
    __Nonexhaustive,
}

impl Error {
    /// Returns `true` if the error was caused by a closed channel.
    pub fn is_closed(&self) -> bool {
        self.is_closed_impl()
    }

    #[cfg(target_os = "fuchsia")]
    fn is_closed_impl(&self) -> bool {
        match self {
            Error::ClientRead(zx_status::Status::PEER_CLOSED)
            | Error::ClientWrite(zx_status::Status::PEER_CLOSED)
            | Error::ClientChannelClosed(_)
            | Error::ServerRequestRead(zx_status::Status::PEER_CLOSED)
            | Error::ServerResponseWrite(zx_status::Status::PEER_CLOSED)
            | Error::ServerEpitaphWrite(zx_status::Status::PEER_CLOSED) => true,
            _ => false,
        }
    }

    #[cfg(not(target_os = "fuchsia"))]
    fn is_closed_impl(&self) -> bool {
        false
    }
}
