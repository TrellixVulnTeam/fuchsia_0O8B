// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    argh::FromArgs,
    failure::{err_msg, Error, ResultExt},
    fidl::endpoints::create_endpoints,
    fidl_fuchsia_bluetooth_avdtp::*,
    fuchsia_async as fasync,
    fuchsia_component::{client, fuchsia_single_component_package_url},
    fuchsia_syslog::{self as syslog, fx_log_info},
    futures::{
        channel::mpsc::{channel, SendError},
        try_join, FutureExt, Sink, SinkExt, Stream, StreamExt, TryStreamExt,
    },
    parking_lot::RwLock,
    pin_utils::pin_mut,
    rustyline::{error::ReadlineError, CompletionType, Config, EditMode, Editor},
    std::{collections::hash_map::Entry, collections::HashMap, sync::Arc, thread},
};

use crate::commands::{Cmd, CmdHelper, ReplControl};
use crate::types::{PeerFactoryMap, CLEAR_LINE, PROMPT};

mod commands;
mod types;

/// Command line arguments that the tool accepts.
/// Currently, the only supported argument at launch is the profile version.
#[derive(FromArgs)]
#[argh(description = "A2DP Profile")]
struct Options {
    /// a2dp profile to use with the tool (values: "sink", "source")
    #[argh(option, short = 'p')]
    profile: String,
}

/// Generates the next local identifier for the current session.
/// Increments linearly, and returns the id as a string.
// TODO(37089): Generate IDs based on the peer_id, and clean up state accordingly.
fn next_string(input: &str) -> String {
    (input.parse::<u64>().unwrap() + 1).to_string()
}

// TODO(37089): Spawn listener for PeerEventStream to delete peer from map
// when a peer disconnects from service.
async fn peer_manager_listener(
    avdtp_svc: &PeerManagerProxy,
    mut stream: PeerManagerEventStream,
    peer_map: Arc<RwLock<PeerFactoryMap>>,
) -> Result<(), Error> {
    let mut id_count = "0".to_string();
    while let Some(evt) = stream.try_next().await? {
        print!("{}", CLEAR_LINE);
        match evt {
            PeerManagerEvent::OnPeerConnected { mut peer_id } => {
                let id: String = format!("{:016x}", peer_id.value);

                let (client, server) = create_endpoints::<PeerControllerMarker>()
                    .expect("Failed to create peer endpoint");
                let peer = client.into_proxy().expect("Error: Couldn't obtain peer client proxy");

                match peer_map.write().entry(id_count.clone()) {
                    Entry::Occupied(_) => {}
                    Entry::Vacant(entry) => {
                        entry.insert(peer);
                        fx_log_info!("Inserted device into PeerFactoryMap");
                    }
                };
                // Establish channel with the given peer_id and server endpoint.
                let _ = avdtp_svc.get_peer(&mut peer_id, server);
                fx_log_info!("Getting peer with peer_id: {} and local id: {}", id, id_count);
                // Increment id count every time a new peer connects.
                id_count = next_string(&id_count);
            }
        }
    }
    Ok(())
}

async fn set_configuration<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.set_configuration().await;
    Ok(String::from("Set configuration command"))
}

async fn get_configuration<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.get_configuration().await;
    Ok(String::from("Get configuration command"))
}

async fn get_capabilities<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.get_capabilities().await;
    Ok(String::from("Get capabilities command"))
}

async fn get_all_capabilities<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.get_all_capabilities().await;
    Ok(String::from("Get all capabilities command"))
}

async fn reconfigure<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.reconfigure_stream().await;
    Ok(String::from("Reconfigure command"))
}

async fn suspend<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.suspend_stream().await;
    Ok(String::from("Suspend command"))
}

async fn suspend_reconfigure<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.suspend_and_reconfigure().await;
    Ok(String::from("Suspend and reconfigure command"))
}

async fn release_stream<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.release_stream().await;
    Ok(String::from("Release stream command"))
}

async fn establish_stream<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.establish_stream().await;
    Ok(String::from("Establish stream command"))
}

async fn abort_stream<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.abort_stream().await;
    Ok(String::from("Abort stream command"))
}

async fn start_stream<'a>(
    _args: &'a [&'a str],
    peer_controller: &PeerControllerProxy,
) -> Result<String, Error> {
    let _ = peer_controller.start_stream().await;
    Ok(String::from("Start stream command"))
}

/// Handle a single raw input command from a user and indicate whether the command should
/// result in continuation or breaking of the read evaluate print loop.
async fn handle_cmd<'a>(
    peer_map: Arc<RwLock<PeerFactoryMap>>,
    line: String,
) -> Result<ReplControl, Error> {
    let components: Vec<_> = line.trim().split_whitespace().collect();
    if components.is_empty() {
        return Ok(ReplControl::Continue);
    }
    let (raw_cmd, args) = components.split_first().expect("not empty");
    let cmd = raw_cmd.parse();
    // TODO(37089): With new ID tracking system, don't need to check this here.
    if args.len() == 0 {
        fx_log_info!("Id missing. Please include peer id with the command.");
        return Ok(ReplControl::Continue);
    }
    let id = args[0];
    let peer_controller = match peer_map.write().entry(id.to_string()) {
        Entry::Occupied(entry) => entry.get().clone(),
        Entry::Vacant(_) => {
            fx_log_info!("No peer established with id: {:?}", id);
            return Ok(ReplControl::Continue);
        }
    };
    let res = match cmd {
        Ok(Cmd::AbortStream) => abort_stream(args, &peer_controller).await,
        Ok(Cmd::EstablishStream) => establish_stream(args, &peer_controller).await,
        Ok(Cmd::ReleaseStream) => release_stream(args, &peer_controller).await,
        Ok(Cmd::StartStream) => start_stream(args, &peer_controller).await,
        Ok(Cmd::SetConfig) => set_configuration(args, &peer_controller).await,
        Ok(Cmd::GetConfig) => get_configuration(args, &peer_controller).await,
        Ok(Cmd::GetCapabilities) => get_capabilities(args, &peer_controller).await,
        Ok(Cmd::GetAllCapabilities) => get_all_capabilities(args, &peer_controller).await,
        Ok(Cmd::Reconfigure) => reconfigure(args, &peer_controller).await,
        Ok(Cmd::Suspend) => suspend(args, &peer_controller).await,
        Ok(Cmd::SuspendReconfigure) => suspend_reconfigure(args, &peer_controller).await,
        Ok(Cmd::Help) => Ok(Cmd::help_msg().to_string()),
        Ok(Cmd::Exit) | Ok(Cmd::Quit) => return Ok(ReplControl::Break),
        Err(_) => {
            fx_log_info!("{} is not a valid command.", raw_cmd);
            Ok(format!("\"{}\" is not a valid command", raw_cmd))
        }
    }?;
    if res != "" {
        println!("{}", res);
    }
    Ok(ReplControl::Continue)
}

/// Generates a rustyline `Editor` in a separate thread to manage user input. This input is returned
/// as a `Stream` of lines entered by the user.
///
/// The thread exits and the `Stream` is exhausted when an error occurs on stdin or the user
/// sends a ctrl-c or ctrl-d sequence.
///
/// Because rustyline shares control over output to the screen with other parts of the system, a
/// `Sink` is passed to the caller to send acknowledgements that a command has been processed and
/// that rustyline should handle the next line of input.
fn cmd_stream() -> (impl Stream<Item = String>, impl Sink<(), Error = SendError>) {
    // Editor thread and command processing thread must be synchronized so that output
    // is printed in the correct order.
    let (mut cmd_sender, cmd_receiver) = channel(512);
    let (ack_sender, mut ack_receiver) = channel(512);

    thread::spawn(move || -> Result<(), Error> {
        let mut exec = fasync::Executor::new().context("error creating readline event loop")?;

        let fut = async {
            let config = Config::builder()
                .auto_add_history(true)
                .history_ignore_space(true)
                .completion_type(CompletionType::List)
                .edit_mode(EditMode::Emacs)
                .build();
            let mut rl: Editor<CmdHelper> = Editor::with_config(config);
            rl.set_helper(Some(CmdHelper::new()));
            loop {
                let readline = rl.readline(PROMPT);
                match readline {
                    Ok(line) => {
                        cmd_sender.try_send(line)?;
                    }
                    Err(ReadlineError::Eof) | Err(ReadlineError::Interrupted) => {
                        return Ok(());
                    }
                    Err(e) => {
                        println!("Error: {:?}", e);
                        return Err(e.into());
                    }
                }
                // wait until processing thread is finished evaluating the last command
                // before running the next loop in the repl
                ack_receiver.next().await;
            }
        };
        exec.run_singlethreaded(fut)
    });
    (cmd_receiver, ack_sender)
}

/// REPL execution
async fn run_repl<'a>(peer_map: Arc<RwLock<PeerFactoryMap>>) -> Result<(), Error> {
    // `cmd_stream` blocks on input in a separate thread and passes commands and acks back to
    // the main thread via async channels.
    let (mut commands, mut acks) = cmd_stream();
    while let Some(cmd) = commands.next().await {
        match handle_cmd(peer_map.clone(), cmd).await {
            Ok(ReplControl::Continue) => {}
            Ok(ReplControl::Break) => {
                println!("\n");
                break;
            }
            Err(e) => {
                println!("Error handling command: {}", e);
            }
        }
        acks.send(()).await?;
    }
    Ok(())
}

#[fasync::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args: Options = argh::from_env();

    syslog::init_with_tags(&["bt-avdtp-tool"]).expect("Can't init logger");

    // Launch the A2DP `profile` locally, and connect to the local service.
    // If an invalid `profile` is given, the tool will exit with an error.
    let launcher = client::launcher().expect("Failed to launch bt-avdtp-tool service");
    let profile = if args.profile == "source".to_string() {
        fuchsia_single_component_package_url!("bt-a2dp-source").to_string()
    } else if args.profile == "sink".to_string() {
        fuchsia_single_component_package_url!("bt-a2dp-sink").to_string()
    } else {
        return Err(err_msg("Invalid A2DP profile. Exiting tool.").into());
    };

    let bt_a2dp = client::launch(&launcher, profile, None)?;
    fx_log_info!("Running bt-avdtp-tool with A2DP {}", &args.profile);

    let avdtp_svc = bt_a2dp
        .connect_to_service::<PeerManagerMarker>()
        .context("Failed to connect to AVDTP Peer Manager")?;
    // Get the list of currently connected peers.
    // Since we are spinning up bt-a2dp-sink locally, there should be no connected peers.
    let _peers = avdtp_svc.connected_peers().await?;

    let evt_stream = avdtp_svc.take_event_stream();
    let peer_map: Arc<RwLock<PeerFactoryMap>> = Arc::new(RwLock::new(HashMap::new()));

    let event_fut = peer_manager_listener(&avdtp_svc, evt_stream, peer_map.clone()).fuse();
    let repl_fut = run_repl(peer_map.clone()).fuse();

    pin_mut!(event_fut);
    pin_mut!(repl_fut);

    try_join!(event_fut, repl_fut).map(|((), ())| ())
}

#[cfg(test)]
mod tests {
    use super::*;

    #[test]
    // Please note finishing bug 37089 will make next_string() obsolete.
    // This bug fix will introduce a better ID management system that is more robust.
    // This is a short term solution to pass PTS certifications.
    fn test_next_string() {
        let id = "0";
        let res = next_string(id);
        assert_eq!("1".to_string(), res);

        let id = "10";
        let res = next_string(id);
        assert_eq!("11".to_string(), res);
    }
}
