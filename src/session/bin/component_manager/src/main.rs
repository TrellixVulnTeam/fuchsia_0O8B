// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

use {
    component_manager_lib::{
        builtin_environment::BuiltinEnvironment,
        model::{
            binding::Binder, model::ComponentManagerConfig, moniker::AbsoluteMoniker,
            testing::test_helpers,
        },
        startup,
    },
    failure::{self, Error},
    fidl::endpoints::ServiceMarker,
    fidl_fuchsia_io::{OPEN_RIGHT_READABLE, OPEN_RIGHT_WRITABLE},
    fidl_fuchsia_session::LauncherMarker,
    fuchsia_component::server::{ServiceFs, ServiceObj},
    futures::prelude::*,
    log::*,
    std::{path::PathBuf, process},
};

/// This is a temporary workaround to allow command line tools to connect to the session manager
/// services, to do things like launch sessions.
///
/// The workaround works as follows:
///
/// The component manager is  started by `sysmgr` with session manager as the root component,
/// by specifying the following in a `sysmgr.config`:
///
/// ```
/// "services": {
///   "some service": [ "component manager url", "session manager url" ]
/// },
/// "startup_services": [
///   "some service"
/// ]
/// ```
///
/// This exposes the service to other components run under `sysmgr` (e.g., a command line tool).
///
/// The session manager exposes the service with the following entry in its `.cml`.
///
/// ```
///
/// "expose": [
///   {
///     "service_protocol": "some service",
///     "from": "self",
///   }
/// ],
/// ```
///
/// This component manager instance then looks for said service in the session manager's
/// `exec/expose/svc` directory, and binds it to the component manager's `out/svc`.
///
/// A command line tool then specifies the following in its `.cml`:
///
/// ```
/// "use": [
///        {
///           "service_protocol": "some service",
///           "from": "realm",
///        }
///    ]
/// ```
///
/// This request is now routed to this component manager via `sysmgr`, as specified in the
/// `sysmgr.config` outlined above.
///
/// Once the session manager no longer needs to run under a component manager which was launched
/// by `sysmgr`, and its services can be routed to command line tools using only component manager
/// routing primitives, this will no longer be needed.

#[fuchsia_async::run_singlethreaded]
async fn main() -> Result<(), Error> {
    let args = match startup::Arguments::from_args() {
        Ok(args) => args,
        Err(err) => {
            error!("{}\n{}", err, startup::Arguments::usage());
            return Err(err);
        }
    };

    let model = startup::model_setup(&args).await?;
    let builtin_environment =
        BuiltinEnvironment::new(&args, &model, ComponentManagerConfig::default()).await?;
    let hub_proxy = builtin_environment.bind_service_fs_for_hub(&model).await?;

    let root_moniker = AbsoluteMoniker::root();
    match model.bind(&root_moniker).await {
        Ok(_) => {
            // TODO: Exit the component manager when the root component's binding is lost
            // (when it terminates).
        }
        Err(error) => {
            error!("Failed to bind to root component: {:?}", error);
            process::exit(1)
        }
    }

    // List the services exposed by the root component (i.e., the session manager).
    let expose_dir_proxy = io_util::open_directory(
        &hub_proxy,
        &PathBuf::from("exec/expose/svc"),
        OPEN_RIGHT_READABLE | OPEN_RIGHT_WRITABLE,
    )
    .expect("Failed to open directory");

    // Make sure the session manager exposes the `fuchsia.session.Launcher` service.
    assert_eq!(
        vec![LauncherMarker::DEBUG_NAME],
        test_helpers::list_directory(&expose_dir_proxy).await
    );

    // Bind the session manager's expose/svc to out/svc of this component, so sysmgr can find it and
    // route service connections to it.
    let mut fs = ServiceFs::<ServiceObj<'_, ()>>::new();
    fs.add_remote("svc", expose_dir_proxy);

    fs.take_and_serve_directory_handle()?;
    fs.collect::<()>().await;
    Ok(())
}
