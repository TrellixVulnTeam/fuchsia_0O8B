// Copyright 2018 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.
#![cfg(test)]

use crate::{AuthProviderSupplier, TokenManagerError};
use async_trait::async_trait;
use failure::Fail;
use fidl::endpoints::{create_request_stream, ClientEnd};
use fidl_fuchsia_auth::{
    AuthProviderMarker, AuthProviderRequest, AuthProviderRequestStream, AuthProviderStatus, Status,
};
use fidl_fuchsia_identity_external::{
    OauthMarker, OauthOpenIdConnectMarker, OauthOpenIdConnectRequest,
    OauthOpenIdConnectRequestStream, OauthRefreshTokenRequest, OauthRequest, OauthRequestStream,
    OpenIdConnectMarker, OpenIdConnectRequest, OpenIdConnectRequestStream,
    OpenIdTokenFromOauthRefreshTokenRequest,
};
use fidl_fuchsia_identity_tokens::OpenIdToken;
use futures::future::join;
use futures::future::BoxFuture;
use futures::prelude::*;
use futures::stream::FuturesUnordered;
use parking_lot::Mutex;
use std::collections::HashMap;
use std::sync::Arc;

/// FakeAuthProviderSupplier implements AuthProviderSupplier, which is needed by TokenManager
/// during instantiation. This fake has a bit of logic: (1) auth provider clients can be
/// pre-populated during test setup, (2) clients will only be given out once through the `get()`
/// method and (3) the `run()` method checks that all clients were given out during the
/// test. The reason for this design is that TokenManager caches auth provider clients and
/// reuses them throughout its lifetime, hence it will never ask for the same auth provider
/// (as defined by its `auth_provider_type`) twice.
pub struct FakeAuthProviderSupplier {
    /// A mapping from auth provider type to client ends of `AuthProvider`
    /// implementations provided in `add_auth_provider`.
    auth_provider_clients: Mutex<HashMap<String, ClientEnd<AuthProviderMarker>>>,
    /// A mapping from auth provider type to client ends of `Oauth`
    /// implementations provided in `add_oauth`.
    oauth_clients: Mutex<HashMap<String, ClientEnd<OauthMarker>>>,
    /// A mapping from auth provider type to client ends of `OauthOpenIdConnect`
    /// implementations provided in `add_oauth_open_id_connect`.
    oauth_open_id_connect_clients: Mutex<HashMap<String, ClientEnd<OauthOpenIdConnectMarker>>>,
    /// A mapping from auth provider type to client ends of `OpenIdConnect`
    /// implementations provided in `add_open_id_connect`.
    open_id_connect_clients: Mutex<HashMap<String, ClientEnd<OpenIdConnectMarker>>>,
    /// The set of Futures that serve added auth provider implementations.
    servers: Mutex<FuturesUnordered<BoxFuture<'static, Result<(), fidl::Error>>>>,
}

#[derive(Debug, Fail)]
pub enum FakeAuthProviderError {
    #[fail(display = "FakeAuthProvider error: A server error occurred: {:?}", _0)]
    ServerError(fidl::Error),
}

impl FakeAuthProviderSupplier {
    pub fn new() -> Self {
        Self {
            auth_provider_clients: Mutex::new(HashMap::new()),
            oauth_clients: Mutex::new(HashMap::new()),
            oauth_open_id_connect_clients: Mutex::new(HashMap::new()),
            open_id_connect_clients: Mutex::new(HashMap::new()),
            servers: Mutex::new(FuturesUnordered::new()),
        }
    }

    /// Add an `AuthProvider` implementation, by supplying two arguments: the type, and a
    /// function which acts as the server end for the auth provider. This function will be
    /// invoked when `run()` is called.
    pub fn add_auth_provider<'a, F, Fut>(&'a self, auth_provider_type: &'a str, server_fn: F)
    where
        F: (FnOnce(AuthProviderRequestStream) -> Fut),
        Fut: Future<Output = Result<(), fidl::Error>> + Send + 'static,
    {
        let (client_end, stream) = create_request_stream::<AuthProviderMarker>().unwrap();
        let serve = server_fn(stream);
        self.auth_provider_clients.lock().insert(auth_provider_type.to_string(), client_end);
        self.servers.lock().push(serve.boxed());
    }

    /// Add an `Oauth` implementation, by supplying two arguments: the type, and a
    /// function which acts as the server end for the auth provider. This function will be
    /// invoked when `run()` is called.
    #[allow(dead_code)]
    pub fn add_oauth<'a, F, Fut>(&'a self, auth_provider_type: &'a str, server_fn: F)
    where
        F: (FnOnce(OauthRequestStream) -> Fut),
        Fut: Future<Output = Result<(), fidl::Error>> + Send + 'static,
    {
        let (client_end, stream) = create_request_stream::<OauthMarker>().unwrap();
        let serve = server_fn(stream);
        self.oauth_clients.lock().insert(auth_provider_type.to_string(), client_end);
        self.servers.lock().push(serve.boxed());
    }

    /// Add an `OpenIdConnect` implementation, by supplying two arguments: the type, and a
    /// function which acts as the server end for the auth provider. This function will be
    /// invoked when `run()` is called.
    #[allow(dead_code)]
    pub fn add_open_id_connect<'a, F, Fut>(&'a self, auth_provider_type: &'a str, server_fn: F)
    where
        F: (FnOnce(OpenIdConnectRequestStream) -> Fut),
        Fut: Future<Output = Result<(), fidl::Error>> + Send + 'static,
    {
        let (client_end, stream) = create_request_stream::<OpenIdConnectMarker>().unwrap();
        let serve = server_fn(stream);
        self.open_id_connect_clients.lock().insert(auth_provider_type.to_string(), client_end);
        self.servers.lock().push(serve.boxed());
    }

    /// Add an `OauthOpenIdConnect` implementation, by supplying two arguments: the type, and a
    /// function which acts as the server end for the auth provider. This function will be
    /// invoked when `run()` is called.
    pub fn add_oauth_open_id_connect<'a, F, Fut>(
        &'a self,
        auth_provider_type: &'a str,
        server_fn: F,
    ) where
        F: (FnOnce(OauthOpenIdConnectRequestStream) -> Fut),
        Fut: Future<Output = Result<(), fidl::Error>> + Send + 'static,
    {
        let (client_end, stream) = create_request_stream::<OauthOpenIdConnectMarker>().unwrap();
        let serve = server_fn(stream);
        self.oauth_open_id_connect_clients
            .lock()
            .insert(auth_provider_type.to_string(), client_end);
        self.servers.lock().push(serve.boxed());
    }

    /// Run the added auth providers to completion. We return an error if a server function
    /// returns an error.  If a server function is never invoked by retrieving the
    /// corresponding ClientEnd using get(), the returned future will never complete.
    /// This is intended to run concurrently with the client code under test.
    pub fn run(&self) -> impl Future<Output = Result<(), FakeAuthProviderError>> {
        let futs = std::mem::replace(&mut *self.servers.lock(), FuturesUnordered::new());
        // This method is not async and instead returns a future so that the future does
        // not need to borrow `self`.  This allows the future to be polled while a
        // concurrent task takes ownership of `self`.
        async move {
            futs.collect::<Vec<_>>()
                .await
                .into_iter()
                .collect::<Result<Vec<_>, fidl::Error>>()
                .map_err(|err| FakeAuthProviderError::ServerError(err))?;
            Ok(())
        }
    }
}

#[async_trait]
impl AuthProviderSupplier for FakeAuthProviderSupplier {
    async fn get_auth_provider(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<AuthProviderMarker>, TokenManagerError> {
        self.auth_provider_clients
            .lock()
            .remove(auth_provider_type)
            .ok_or(TokenManagerError::new(Status::AuthProviderServiceUnavailable))
    }

    async fn get_oauth(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthMarker>, TokenManagerError> {
        self.oauth_clients
            .lock()
            .remove(auth_provider_type)
            .ok_or(TokenManagerError::new(Status::AuthProviderServiceUnavailable))
    }

    async fn get_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OpenIdConnectMarker>, TokenManagerError> {
        self.open_id_connect_clients
            .lock()
            .remove(auth_provider_type)
            .ok_or(TokenManagerError::new(Status::AuthProviderServiceUnavailable))
    }

    async fn get_oauth_open_id_connect(
        &self,
        auth_provider_type: &str,
    ) -> Result<ClientEnd<OauthOpenIdConnectMarker>, TokenManagerError> {
        self.oauth_open_id_connect_clients
            .lock()
            .remove(auth_provider_type)
            .ok_or(TokenManagerError::new(Status::AuthProviderServiceUnavailable))
    }
}

mod tests {
    use super::*;
    use fuchsia_async;

    /// This is a meta-test, of the FakeAuthProviderSupplier itself, since it has a bit of logic.
    /// First we create a FakeAuthProviderSupplier with two auth providers, Hooli and Pied Piper,
    /// for which we create two servers with expects on the incoming messages, and pre-populated
    /// responses. Finally, we create a client that retrieves the auth providers from the fake,
    /// sends the expected messages to them and checks that the responses match.
    #[fuchsia_async::run_until_stalled(test)]
    async fn auth_provider_fake_test() {
        let auth_provider_supplier = Arc::new(FakeAuthProviderSupplier::new());
        // Non existing auth provider
        assert!(auth_provider_supplier.get_auth_provider("myspace").await.is_err());

        auth_provider_supplier.add_auth_provider("hooli", |mut stream| async move {
            match stream.try_next().await? {
                Some(AuthProviderRequest::RevokeAppOrPersistentCredential {
                    responder,
                    credential,
                }) => {
                    assert_eq!(credential, "HOOLI_CREDENTIAL");
                    responder.send(AuthProviderStatus::BadRequest)?;
                }
                _ => panic!("Unexpected message received"),
            }
            assert!(stream.try_next().await?.is_none());
            Ok(())
        });

        auth_provider_supplier.add_auth_provider("pied-piper", |mut stream| async move {
            match stream.try_next().await? {
                Some(AuthProviderRequest::GetAppIdToken { responder, credential, audience }) => {
                    assert_eq!(credential, "PIED_PIPER_CREDENTIAL");
                    assert!(audience.is_none());
                    responder.send(AuthProviderStatus::Ok, None)?;
                }
                _ => panic!("Unexpected message received"),
            }
            assert!(stream.try_next().await?.is_none());
            Ok(())
        });

        let auth_provider_supplier_clone = Arc::clone(&auth_provider_supplier);

        let client_fn = async move {
            let ap_proxy = auth_provider_supplier_clone
                .get_auth_provider("hooli")
                .await
                .unwrap()
                .into_proxy()
                .unwrap();
            let status =
                ap_proxy.revoke_app_or_persistent_credential("HOOLI_CREDENTIAL").await.unwrap();
            assert_eq!(status, AuthProviderStatus::BadRequest);

            let ap_proxy = auth_provider_supplier_clone
                .get_auth_provider("pied-piper")
                .await
                .unwrap()
                .into_proxy()
                .unwrap();
            let (status, auth_token) =
                ap_proxy.get_app_id_token("PIED_PIPER_CREDENTIAL", None).await.unwrap();
            assert_eq!(status, AuthProviderStatus::Ok);
            assert!(auth_token.is_none());
        };
        let (run_result, _) = join(auth_provider_supplier.run(), client_fn).await;
        assert!(run_result.is_ok());
        assert!(auth_provider_supplier.get_auth_provider("hooli").await.is_err());
        assert!(auth_provider_supplier.get_auth_provider("pied-piper").await.is_err());
    }

    /// A test for FakeAuthProviderSupplier that walks through setting and retrieving one of
    /// each possible protocol.
    #[fuchsia_async::run_until_stalled(test)]
    async fn multiple_protocols_test() {
        let auth_provider_supplier = FakeAuthProviderSupplier::new();

        auth_provider_supplier.add_auth_provider("hooli", |mut stream| async move {
            match stream.try_next().await? {
                Some(AuthProviderRequest::RevokeAppOrPersistentCredential {
                    responder, ..
                }) => {
                    responder.send(AuthProviderStatus::BadRequest)?;
                }
                _ => panic!("Unexpected message received"),
            }
            assert!(stream.try_next().await?.is_none());
            Ok(())
        });

        auth_provider_supplier.add_oauth("hooli", |mut stream| async move {
            match stream.try_next().await? {
                Some(OauthRequest::CreateRefreshToken { responder, .. }) => {
                    responder
                        .send(&mut Err(fidl_fuchsia_identity_external::Error::InvalidRequest))?;
                }
                _ => panic!("Unexpected message received"),
            }
            assert!(stream.try_next().await?.is_none());
            Ok(())
        });

        auth_provider_supplier.add_oauth_open_id_connect("hooli", |mut stream| async move {
            match stream.try_next().await? {
                Some(OauthOpenIdConnectRequest::GetIdTokenFromRefreshToken {
                    responder, ..
                }) => {
                    responder
                        .send(&mut Err(fidl_fuchsia_identity_external::Error::InvalidRequest))?;
                }
                _ => panic!("Unexpected message received"),
            }
            assert!(stream.try_next().await?.is_none());
            Ok(())
        });

        auth_provider_supplier.add_open_id_connect("hooli", |mut stream| async move {
            match stream.try_next().await? {
                Some(OpenIdConnectRequest::RevokeIdToken { responder, .. }) => {
                    responder
                        .send(&mut Err(fidl_fuchsia_identity_external::Error::InvalidRequest))?;
                }
                _ => panic!("Unexpected message received"),
            }
            assert!(stream.try_next().await?.is_none());
            Ok(())
        });

        let client_fn = async {
            let auth_provider_proxy = auth_provider_supplier
                .get_auth_provider("hooli")
                .await
                .unwrap()
                .into_proxy()
                .unwrap();
            assert_eq!(
                auth_provider_proxy
                    .revoke_app_or_persistent_credential("HOOLI_CREDENTIAL")
                    .await
                    .unwrap(),
                AuthProviderStatus::BadRequest
            );

            let oauth_proxy =
                auth_provider_supplier.get_oauth("hooli").await.unwrap().into_proxy().unwrap();

            assert_eq!(
                oauth_proxy
                    .create_refresh_token(OauthRefreshTokenRequest {
                        account_id: None,
                        ui_context: None
                    })
                    .await
                    .unwrap(),
                Err(fidl_fuchsia_identity_external::Error::InvalidRequest)
            );

            let oauth_open_id_connect_proxy = auth_provider_supplier
                .get_oauth_open_id_connect("hooli")
                .await
                .unwrap()
                .into_proxy()
                .unwrap();

            assert_eq!(
                oauth_open_id_connect_proxy
                    .get_id_token_from_refresh_token(OpenIdTokenFromOauthRefreshTokenRequest {
                        refresh_token: None,
                        audiences: None,
                    })
                    .await
                    .unwrap(),
                Err(fidl_fuchsia_identity_external::Error::InvalidRequest)
            );

            let open_id_connect_proxy = auth_provider_supplier
                .get_open_id_connect("hooli")
                .await
                .unwrap()
                .into_proxy()
                .unwrap();

            assert_eq!(
                open_id_connect_proxy
                    .revoke_id_token(OpenIdToken { content: None, expiry_time: None })
                    .await
                    .unwrap(),
                Err(fidl_fuchsia_identity_external::Error::InvalidRequest)
            );
        };

        let (run_result, _) = join(auth_provider_supplier.run(), client_fn).await;
        assert!(run_result.is_ok());
    }
}
