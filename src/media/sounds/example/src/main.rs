// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#![recursion_limit = "512"]

use failure::{Error, ResultExt};
use fidl_fuchsia_media::*;
use fidl_fuchsia_media_sounds::*;
use fuchsia_async as fasync;
use fuchsia_component as component;
use fuchsia_zircon::{self as zx, Vmo};
use futures::{join, FutureExt};
use std::{convert::TryInto, fs::File};
use zerocopy::AsBytes;

type Result<T> = std::result::Result<T, Error>;

#[fasync::run_singlethreaded]
async fn main() -> Result<()> {
    let player_proxy = component::client::connect_to_service::<PlayerMarker>()
        .context("Connecting to fuchsia.media.sounds.Player")?;

    player_proxy
        .add_sound_from_file(0, resource_file_channel("sfx.wav")?)
        .await?
        .map_err(|status| failure::format_err!("AddSoundFromFile failed {}", status))?;

    let duration = std::time::Duration::from_secs(1);

    let (mut buffer, mut stream_type) = sound_in_buffer(-8, 0.5, 0.9, duration)?;
    player_proxy.add_sound_buffer(1, &mut buffer, &mut stream_type)?;

    let (mut buffer, mut stream_type) = sound_in_buffer(-17, 0.5, 0.9, duration)?;
    player_proxy.add_sound_buffer(2, &mut buffer, &mut stream_type)?;

    let (mut buffer, mut stream_type) = sound_in_buffer(-12, 0.5, 0.9, duration)?;
    player_proxy.add_sound_buffer(3, &mut buffer, &mut stream_type)?;

    // Play the file-based sound.
    player_proxy
        .play_sound(0, AudioRenderUsage::Media)
        .await?
        .map_err(|err| failure::format_err!("PlaySound failed: {:?}", err))?;

    // Play the VMO-based sounds in sequence.
    player_proxy
        .play_sound(1, AudioRenderUsage::Media)
        .await?
        .map_err(|err| failure::format_err!("PlaySound failed: {:?}", err))?;
    player_proxy
        .play_sound(2, AudioRenderUsage::Media)
        .await?
        .map_err(|err| failure::format_err!("PlaySound failed: {:?}", err))?;
    player_proxy
        .play_sound(3, AudioRenderUsage::Media)
        .await?
        .map_err(|err| failure::format_err!("PlaySound failed: {:?}", err))?;

    // Play the VMO-based sounds all at once.
    join!(
        player_proxy.play_sound(1, AudioRenderUsage::Media).map(|_| ()),
        player_proxy.play_sound(2, AudioRenderUsage::Media).map(|_| ()),
        player_proxy.play_sound(3, AudioRenderUsage::Media).map(|_| ())
    );

    Ok(())
}

/// Creates a file channel from a resource file.
fn resource_file_channel(name: &str) -> Result<zx::Channel> {
    // We try two paths here, because normal components see their package data resources in
    // /pkg/data and shell tools see them in /pkgfs/packages/<pkg>>/0/data.
    Ok(zx::Channel::from(fdio::transfer_fd(
        File::open(format!("/pkg/data/{}", name))
            .or_else(|_| File::open(format!("/pkgfs/packages/soundplayer_example/0/data/{}", name)))
            .context("Opening package data file")?,
    )?))
}

/// Creates a VMO-based sound containing a decaying sine wave using the slope iteration method.
/// `note` is in semitones with 0 being concert A (440Hz).
fn sound_in_buffer(
    note: i32,
    volume: f32,
    decay: f32,
    duration: std::time::Duration,
) -> Result<(fidl_fuchsia_mem::Buffer, AudioStreamType)> {
    const FRAMES_PER_SECOND: u32 = 44100;

    let frequency = frequency_from_note(note);
    let frame_count = (FRAMES_PER_SECOND as f32 * duration.as_secs_f32()) as usize;
    let rot_coeff = (2.0 * std::f32::consts::PI * frequency) / (FRAMES_PER_SECOND as f32);
    let decay_factor = (1.0 - decay).powf(1.0 / FRAMES_PER_SECOND as f32);

    let mut real_sample: f32 = 0.0;
    let mut imaginary_sample: f32 = 32760.0;
    let mut v = volume;

    let mut samples = std::vec::Vec::with_capacity(frame_count);
    for _i in 0..frame_count {
        samples.push((real_sample * v) as i16);

        // Rotate real_sample,imaginary_sample around the origin.
        real_sample -= imaginary_sample * rot_coeff;
        imaginary_sample += real_sample * rot_coeff;

        v = v * decay_factor;
    }

    let vmo = Vmo::create((frame_count * 2) as u64).context("Creating VMO")?;
    vmo.write(&samples.as_bytes(), 0).context("Writing to VMO")?;

    Ok((
        fidl_fuchsia_mem::Buffer { vmo: vmo, size: (frame_count * 2).try_into()? },
        AudioStreamType {
            sample_format: AudioSampleFormat::Signed16,
            channels: 1,
            frames_per_second: FRAMES_PER_SECOND,
        },
    ))
}

/// Calculates frequency from a note number. We multiply concert A frequency (440Hz) by the
/// twelfth root of two to the 'note' power.
fn frequency_from_note(note: i32) -> f32 {
    const CONCERT_A_FREQUENCY: f32 = 440.0;
    return CONCERT_A_FREQUENCY * 2.0_f32.powf(note as f32 / 12.0);
}
