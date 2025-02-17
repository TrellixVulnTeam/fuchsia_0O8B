// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    failure::{format_err, Error},
    fidl::{
        encoding::Decodable as FidlDecodable,
        endpoints::{RequestStream, ServiceMarker},
    },
    fidl_fuchsia_bluetooth_avrcp::*,
    fidl_fuchsia_bluetooth_avrcp_test::*,
    fuchsia_async as fasync,
    fuchsia_component::server::ServiceFs,
    fuchsia_syslog::{self, fx_log_err, fx_log_info, fx_log_warn},
    fuchsia_zircon as zx,
    futures::{
        self,
        channel::mpsc,
        future::{FutureExt, TryFutureExt},
        stream::{StreamExt, TryStreamExt},
        Future,
    },
    std::collections::VecDeque,
};

use crate::{
    packets::PlaybackStatus as PacketPlaybackStatus,
    peer::{
        Controller, ControllerEvent as PeerControllerEvent,
        ControllerRequest as PeerControllerRequest,
    },
    types::PeerError,
};

impl From<PeerError> for ControllerError {
    fn from(e: PeerError) -> Self {
        match e {
            PeerError::PacketError(_) => ControllerError::PacketEncoding,
            PeerError::AvctpError(_) => ControllerError::ProtocolError,
            PeerError::RemoteNotFound => ControllerError::RemoteNotConnected,
            PeerError::CommandNotSupported => ControllerError::CommandNotImplemented,
            PeerError::CommandFailed => ControllerError::UnkownFailure,
            PeerError::ConnectionFailure(_) => ControllerError::ConnectionError,
            PeerError::UnexpectedResponse => ControllerError::UnexpectedResponse,
            _ => ControllerError::UnkownFailure,
        }
    }
}

/// FIDL wrapper for a internal PeerController.
struct AvrcpClientController {
    controller: Controller,
    fidl_stream: ControllerRequestStream,
    notification_filter: Notifications,
    position_change_interval: u32,
    notification_window_counter: u32,
    notification_queue: VecDeque<(i64, PeerControllerEvent)>,
}

impl AvrcpClientController {
    const EVENT_WINDOW_LIMIT: u32 = 3;

    fn new(controller: Controller, fidl_stream: ControllerRequestStream) -> Self {
        Self {
            controller,
            fidl_stream,
            notification_filter: Notifications::empty(),
            position_change_interval: 0,
            notification_window_counter: 0,
            notification_queue: VecDeque::new(),
        }
    }

    async fn handle_fidl_request(&mut self, request: ControllerRequest) -> Result<(), Error> {
        match request {
            ControllerRequest::GetPlayerApplicationSettings { attribute_ids, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .get_player_application_settings(
                            attribute_ids.into_iter().map(|x| x.into()).collect(),
                        )
                        .await
                        .map(|res| res.into())
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::SetPlayerApplicationSettings { requested_settings, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .set_player_application_settings(requested_settings.into())
                        .await
                        .map(|res| res.into())
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::GetMediaAttributes { responder } => {
                responder.send(
                    &mut self
                        .controller
                        .get_media_attributes()
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::GetPlayStatus { responder } => {
                responder.send(
                    &mut self.controller.get_play_status().await.map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::InformBatteryStatus { battery_status: _, responder } => {
                responder.send(&mut Err(ControllerError::CommandNotImplemented))?;
            }
            ControllerRequest::SetNotificationFilter {
                notifications,
                position_change_interval,
                control_handle: _,
            } => {
                self.notification_filter = notifications;
                self.position_change_interval = position_change_interval;
            }
            ControllerRequest::NotifyNotificationHandled { control_handle: _ } => {
                debug_assert!(self.notification_window_counter != 0);
                if self.notification_window_counter > 0 {
                    self.notification_window_counter -= 1;
                    match self.notification_queue.pop_front() {
                        Some((timestamp, event)) => {
                            self.handle_controller_event(timestamp, event)?;
                        }
                        None => {}
                    }
                }
            }
            ControllerRequest::SetAddressedPlayer { player_id: _, responder } => {
                responder.send(&mut Err(ControllerError::CommandNotImplemented))?;
            }
            ControllerRequest::SetAbsoluteVolume { requested_volume, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .set_absolute_volume(requested_volume)
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
            ControllerRequest::SendCommand { command, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .send_keypress(command.into_primitive())
                        .await
                        .map_err(ControllerError::from),
                )?;
            }
        };
        Ok(())
    }

    fn handle_controller_event(
        &mut self,
        timestamp: i64,
        event: PeerControllerEvent,
    ) -> Result<(), Error> {
        self.notification_window_counter += 1;
        let control_handle: ControllerControlHandle = self.fidl_stream.control_handle();
        match event {
            PeerControllerEvent::PlaybackStatusChanged(playback_status) => {
                let status = match playback_status {
                    PacketPlaybackStatus::Stopped => PlaybackStatus::Stopped,
                    PacketPlaybackStatus::Playing => PlaybackStatus::Playing,
                    PacketPlaybackStatus::Paused => PlaybackStatus::Paused,
                    PacketPlaybackStatus::FwdSeek => PlaybackStatus::FwdSeek,
                    PacketPlaybackStatus::RevSeek => PlaybackStatus::RevSeek,
                    PacketPlaybackStatus::Error => PlaybackStatus::Error,
                };

                let notification =
                    Notification { status: Some(status), ..Notification::new_empty() };
                control_handle.send_on_notification(timestamp, notification).map_err(Error::from)
            }
            PeerControllerEvent::TrackIdChanged(track_id) => {
                let notification =
                    Notification { track_id: Some(track_id), ..Notification::new_empty() };
                control_handle.send_on_notification(timestamp, notification).map_err(Error::from)
            }
            PeerControllerEvent::PlaybackPosChanged(pos) => {
                let notification = Notification { pos: Some(pos), ..Notification::new_empty() };
                control_handle.send_on_notification(timestamp, notification).map_err(Error::from)
            }
        }
    }

    /// Returns true if the event should be dispatched.
    fn filter_controller_event(&self, event: &PeerControllerEvent) -> bool {
        match *event {
            PeerControllerEvent::PlaybackStatusChanged(_) => {
                self.notification_filter.contains(Notifications::PlaybackStatus)
            }
            PeerControllerEvent::TrackIdChanged(_) => {
                self.notification_filter.contains(Notifications::Track)
            }
            PeerControllerEvent::PlaybackPosChanged(_) => {
                self.notification_filter.contains(Notifications::TrackPos)
            }
        }
    }

    async fn run(&mut self) -> Result<(), Error> {
        let mut controller_events = self.controller.take_event_stream();
        loop {
            futures::select! {
                req = self.fidl_stream.select_next_some() => {
                    self.handle_fidl_request(req?).await?;
                }
                event = controller_events.select_next_some() => {
                    if self.filter_controller_event(&event) {
                        let timestamp = zx::Time::get(zx::ClockId::UTC).into_nanos();
                        if self.notification_window_counter > Self::EVENT_WINDOW_LIMIT {
                            self.notification_queue.push_back((timestamp, event));
                        } else {
                            self.handle_controller_event(timestamp, event)?;
                        }
                    }
                }
                complete => { return Ok(()); }
            }
        }
    }
}

/// FIDL wrapper for a internal PeerController for the test (ControllerExt) interface methods.
struct TestAvrcpClientController {
    controller: Controller,
    fidl_stream: ControllerExtRequestStream,
}

impl TestAvrcpClientController {
    async fn handle_fidl_request(&self, request: ControllerExtRequest) -> Result<(), Error> {
        match request {
            ControllerExtRequest::IsConnected { responder } => {
                responder.send(self.controller.is_connected())?;
            }
            ControllerExtRequest::GetEventsSupported { responder } => {
                match self.controller.get_supported_events().await {
                    Ok(events) => {
                        let mut r_events = vec![];
                        for e in events {
                            if let Some(target_event) =
                                NotificationEvent::from_primitive(u8::from(&e))
                            {
                                r_events.push(target_event);
                            }
                        }
                        responder.send(&mut Ok(r_events))?;
                    }
                    Err(peer_error) => {
                        responder.send(&mut Err(ControllerError::from(peer_error)))?
                    }
                }
            }
            ControllerExtRequest::Connect { control_handle: _ } => {
                // TODO(37266): implement
            }
            ControllerExtRequest::Disconnect { control_handle: _ } => {
                // TODO(37266): implement
            }
            ControllerExtRequest::SendRawVendorDependentCommand { pdu_id, command, responder } => {
                responder.send(
                    &mut self
                        .controller
                        .send_raw_vendor_command(pdu_id, &command[..])
                        .map_err(|e| ControllerError::from(e))
                        .await,
                )?;
            }
        };
        Ok(())
    }

    async fn run(&mut self) -> Result<(), Error> {
        loop {
            futures::select! {
                req = self.fidl_stream.select_next_some() => {
                    self.handle_fidl_request(req?).await?;
                }
                complete => { return Ok(()); }
            }
        }
    }
}

/// Spawns a future that facilitates communication between a PeerController and a FIDL client.
pub fn spawn_avrcp_client_controller(controller: Controller, fidl_stream: ControllerRequestStream) {
    fasync::spawn(
        async move {
            let mut acc = AvrcpClientController::new(controller, fidl_stream);
            acc.run().await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

/// Spawns a future that facilitates communication between a PeerController and a test FIDL client.
pub fn spawn_test_avrcp_client_controller(
    controller: Controller,
    fidl_stream: ControllerExtRequestStream,
) {
    fasync::spawn(
        async move {
            let mut acc = TestAvrcpClientController { controller, fidl_stream };
            acc.run().await?;
            Ok(())
        }
        .boxed()
        .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

/// Spawns a future that listens and responds to requests for a controller object over FIDL.
fn spawn_avrcp_client(
    stream: PeerManagerRequestStream,
    sender: mpsc::Sender<PeerControllerRequest>,
) {
    fx_log_info!("Spawning avrcp client handler");
    fasync::spawn(
        avrcp_client_stream_handler(stream, sender, &spawn_avrcp_client_controller)
            .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

/// Polls the stream for the PeerManager FIDL interface and responds with new controller clients.
pub async fn avrcp_client_stream_handler<F>(
    mut stream: PeerManagerRequestStream,
    mut sender: mpsc::Sender<PeerControllerRequest>,
    spawn_fn: F,
) -> Result<(), failure::Error>
where
    F: Fn(Controller, ControllerRequestStream),
{
    while let Some(PeerManagerRequest::GetControllerForTarget { peer_id, client, responder }) =
        stream.try_next().await?
    {
        let client: fidl::endpoints::ServerEnd<ControllerMarker> = client;

        fx_log_info!("New connection request for {}", peer_id);

        match client.into_stream() {
            Err(err) => {
                fx_log_warn!("Err unable to create server end point from stream {:?}", err);
                responder.send(&mut Err(zx::Status::UNAVAILABLE.into_raw()))?;
            }
            Ok(client_stream) => {
                let (response, pcr) = PeerControllerRequest::new(peer_id);
                sender.try_send(pcr)?;
                let controller = response.into_future().await?;
                spawn_fn(controller, client_stream);
                responder.send(&mut Ok(()))?;
            }
        }
    }
    Ok(())
}

/// spawns a future that listens and responds to requests for a controller object over FIDL.
fn spawn_test_avrcp_client(
    stream: PeerManagerExtRequestStream,
    sender: mpsc::Sender<PeerControllerRequest>,
) {
    fx_log_info!("Spawning test avrcp client handler");
    fasync::spawn(
        test_avrcp_client_stream_handler(stream, sender, &spawn_test_avrcp_client_controller)
            .unwrap_or_else(|e: failure::Error| fx_log_err!("{:?}", e)),
    );
}

/// polls the stream for the PeerManagerExt FIDL interface and responds with new test controller clients.
pub async fn test_avrcp_client_stream_handler<F>(
    mut stream: PeerManagerExtRequestStream,
    mut sender: mpsc::Sender<PeerControllerRequest>,
    spawn_fn: F,
) -> Result<(), failure::Error>
where
    F: Fn(Controller, ControllerExtRequestStream),
{
    while let Some(req) = stream.try_next().await? {
        match req {
            PeerManagerExtRequest::GetControllerForTarget { peer_id, client, responder } => {
                let client: fidl::endpoints::ServerEnd<ControllerExtMarker> = client;

                fx_log_info!("New connection request for {}", peer_id);

                match client.into_stream() {
                    Err(err) => {
                        fx_log_warn!("Err unable to create server end point from stream {:?}", err);
                        responder.send(&mut Err(zx::Status::UNAVAILABLE.into_raw()))?;
                    }
                    Ok(client_stream) => {
                        let (response, pcr) = PeerControllerRequest::new(peer_id);
                        sender.try_send(pcr)?;
                        let controller = response.into_future().await?;
                        spawn_fn(controller, client_stream);
                        responder.send(&mut Ok(()))?;
                    }
                }
            }
        }
    }
    Ok(())
}

/// Sets up public FIDL services and client handlers.
pub fn run_services(
    sender: mpsc::Sender<PeerControllerRequest>,
) -> Result<impl Future<Output = Result<(), Error>>, Error> {
    let mut fs = ServiceFs::new();
    let sender_avrcp = sender.clone();
    let sender_test = sender.clone();
    fs.dir("svc")
        .add_fidl_service_at(PeerManagerExtMarker::NAME, move |stream| {
            spawn_test_avrcp_client(stream, sender_test.clone());
        })
        .add_fidl_service_at(PeerManagerMarker::NAME, move |stream| {
            spawn_avrcp_client(stream, sender_avrcp.clone());
        });
    fs.take_and_serve_directory_handle()?;
    fx_log_info!("Running fidl service");
    Ok(fs.collect::<()>().map(|_| Err(format_err!("FIDL service listener returned"))))
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::peer::PeerManager;
    use crate::tests::{create_fidl_endpoints, MockProfileService};
    use failure::format_err;
    use fidl::endpoints::create_endpoints;
    use futures::{future, future::FutureExt};
    use parking_lot::Mutex;
    use pin_utils::pin_mut;
    use std::collections::VecDeque;

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_spawn_avrcp_client() -> Result<(), Error> {
        let (client, server): (PeerManagerProxy, PeerManagerRequestStream) =
            create_fidl_endpoints::<PeerManagerMarker>()?;

        let control_handle = server.control_handle();
        let (client_sender, peer_controller_request_receiver) = mpsc::channel(512);

        let profile_service = MockProfileService { fake_events: Mutex::new(Some(VecDeque::new())) };

        let test_fn = |_controller: Controller, _fidl_stream: ControllerRequestStream| {
            control_handle.shutdown();
        };

        let mut peer_manager =
            PeerManager::new(Box::new(profile_service), peer_controller_request_receiver)
                .expect("unable to create peer manager");
        let (_c_client, c_server) =
            create_endpoints::<ControllerMarker>().expect("Error creating Controller endpoint");

        let handler_fut = future::join(
            avrcp_client_stream_handler(server, client_sender.clone(), test_fn),
            client.get_controller_for_target(&"123", c_server),
        )
        .fuse();
        pin_mut!(handler_fut);

        let peer_manager_run_fut = peer_manager.run().fuse();
        pin_mut!(peer_manager_run_fut);

        futures::select! {
            _= peer_manager_run_fut  => {
                return Err(format_err!("peer manager returned early"))
            }
            res = handler_fut => {
                println!("handler_fut fired");
                drop(client);
                res.0.expect("error with spawning handler");
                let _ = res.1.expect("error get_controller_for_target");
                return Ok(())
            }
        }
    }

    #[fuchsia_async::run_singlethreaded(test)]
    async fn test_spawn_test_avrcp_client() -> Result<(), Error> {
        let (client, server): (PeerManagerExtProxy, PeerManagerExtRequestStream) =
            create_fidl_endpoints::<PeerManagerExtMarker>()?;

        let control_handle = server.control_handle();
        let (client_sender, peer_controller_request_receiver) = mpsc::channel(512);

        let profile_service = MockProfileService { fake_events: Mutex::new(Some(VecDeque::new())) };

        let test_fn = |_controller: Controller, _fidl_stream: ControllerExtRequestStream| {
            control_handle.shutdown();
        };

        let mut peer_manager =
            PeerManager::new(Box::new(profile_service), peer_controller_request_receiver)
                .expect("unable to create peer manager");
        let (_t_client, t_server) = create_endpoints::<ControllerExtMarker>()
            .expect("Error creating Test Controller endpoint");

        let handler_fut = future::join(
            test_avrcp_client_stream_handler(server, client_sender.clone(), test_fn),
            client.get_controller_for_target(&"123", t_server),
        )
        .fuse();
        pin_mut!(handler_fut);

        let peer_manager_run_fut = peer_manager.run().fuse();
        pin_mut!(peer_manager_run_fut);

        futures::select! {
            _= peer_manager_run_fut  => {
                return Err(format_err!("peer manager returned early"))
            }
            res = handler_fut => {
                println!("handler_fut fired");
                drop(client);
                res.0.expect("error with spawning handler");
                let _ = res.1.expect("error get_test_controller_for_target");
                return Ok(())
            }
        }
    }
}
