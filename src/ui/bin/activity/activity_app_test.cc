// Copyright 2019 The Fuchsia Authors. All rights reserved.
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "src/ui/bin/activity/activity_app.h"

#include <fuchsia/ui/activity/cpp/fidl.h>

#include <memory>

#include "garnet/public/lib/gtest/test_loop_fixture.h"
#include "src/ui/bin/activity/fake_listener.h"
#include "src/ui/bin/activity/state_machine_driver.h"

namespace {

fuchsia::ui::activity::DiscreteActivity DiscreteActivity() {
  fuchsia::ui::activity::GenericActivity generic;
  fuchsia::ui::activity::DiscreteActivity activity;
  activity.set_generic(std::move(generic));
  return activity;
};

fuchsia::ui::activity::OngoingActivity OngoingActivity() {
  fuchsia::ui::activity::GenericActivity generic;
  fuchsia::ui::activity::OngoingActivity activity;
  activity.set_generic(std::move(generic));
  return activity;
};

}  // namespace

namespace activity {

class ActivityAppTest : public ::gtest::TestLoopFixture {
 public:
  ActivityAppTest() = default;

  void SetUp() override {
    auto driver = std::make_unique<StateMachineDriver>(dispatcher());
    driver_ = driver.get();
    app_ = std::make_unique<ActivityApp>(std::move(driver), dispatcher());
  }

 protected:
  std::unique_ptr<ActivityApp> app_;
  const StateMachineDriver* driver_;
};

namespace {

TEST_F(ActivityAppTest, Tracker_ConnectDisconnect) {
  {
    fuchsia::ui::activity::TrackerPtr tracker;
    app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_->tracker_bindings().size(), 0u);
}

TEST_F(ActivityAppTest, Tracker_Multiple_ConnectDisconnect) {
  {
    fuchsia::ui::activity::TrackerPtr tracker1, tracker2;
    app_->AddTrackerBinding(tracker1.NewRequest(dispatcher()));
    app_->AddTrackerBinding(tracker2.NewRequest(dispatcher()));
    RunLoopUntilIdle();
    EXPECT_EQ(app_->tracker_bindings().size(), 2u);
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_->tracker_bindings().size(), 0u);
}

TEST_F(ActivityAppTest, Tracker_SendActivity) {
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));

  ASSERT_EQ(driver_->GetState(), fuchsia::ui::activity::State::IDLE);
  tracker->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  RunLoopUntilIdle();
  EXPECT_EQ(driver_->GetState(), fuchsia::ui::activity::State::ACTIVE);
}

TEST_F(ActivityAppTest, Tracker_OngoingActivity) {
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));
  ASSERT_EQ(driver_->GetState(), fuchsia::ui::activity::State::IDLE);
  OngoingActivityId id;

  tracker->StartOngoingActivity(OngoingActivity(), Now().get(),
                                [&id](OngoingActivityId returned_id) { id = returned_id; });
  RunLoopUntilIdle();
  EXPECT_EQ(driver_->GetState(), fuchsia::ui::activity::State::ACTIVE);

  auto timeout = driver_->state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);
  // No state change expected after timeout since there is an ongoing activity
  EXPECT_EQ(driver_->GetState(), fuchsia::ui::activity::State::ACTIVE);

  tracker->EndOngoingActivity(id, Now().get());
  RunLoopFor(*timeout);
  EXPECT_EQ(driver_->GetState(), fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityAppTest, Provider_ConnectDisconnect) {
  {
    fuchsia::ui::activity::ProviderPtr provider;
    app_->AddProviderBinding(provider.NewRequest(dispatcher()));

    testing::FakeListener listener;
    provider->WatchState(listener.NewHandle(dispatcher()));
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_->provider_bindings().size(), 0u);
}

TEST_F(ActivityAppTest, Provider_ReceivesInitialState) {
  fuchsia::ui::activity::ProviderPtr provider;
  app_->AddProviderBinding(provider.NewRequest(dispatcher()));

  testing::FakeListener listener;
  provider->WatchState(listener.NewHandle(dispatcher()));
  RunLoopUntilIdle();

  EXPECT_EQ(listener.StateChanges().size(), 1u);
  EXPECT_EQ(listener.StateChanges().front().state, fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityAppTest, Provider_ReceivesSubsequentStates) {
  fuchsia::ui::activity::ProviderPtr provider;
  app_->AddProviderBinding(provider.NewRequest(dispatcher()));
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));

  testing::FakeListener listener;
  provider->WatchState(listener.NewHandle(dispatcher()));
  RunLoopUntilIdle();

  tracker->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  auto timeout = driver_->state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  ASSERT_EQ(listener.StateChanges().size(), 3u);
  EXPECT_EQ(listener.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(listener.StateChanges()[2].state, fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityAppTest, Provider_MultipleProviders_ConnectDisconnect) {
  {
    fuchsia::ui::activity::ProviderPtr provider1, provider2;
    app_->AddProviderBinding(provider1.NewRequest(dispatcher()));
    app_->AddProviderBinding(provider2.NewRequest(dispatcher()));

    testing::FakeListener listener1, listener2;
    provider1->WatchState(listener1.NewHandle(dispatcher()));
    provider2->WatchState(listener2.NewHandle(dispatcher()));
    RunLoopUntilIdle();
    EXPECT_EQ(app_->provider_bindings().size(), 2u);
  }
  RunLoopUntilIdle();

  EXPECT_EQ(app_->provider_bindings().size(), 0u);
}

TEST_F(ActivityAppTest, Provider_MultipleProviders_AllReceiveState) {
  fuchsia::ui::activity::ProviderPtr provider1, provider2;
  app_->AddProviderBinding(provider1.NewRequest(dispatcher()));
  app_->AddProviderBinding(provider2.NewRequest(dispatcher()));
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));

  testing::FakeListener listener1, listener2;
  provider1->WatchState(listener1.NewHandle(dispatcher()));
  provider2->WatchState(listener2.NewHandle(dispatcher()));
  RunLoopUntilIdle();

  tracker->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  auto timeout = driver_->state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  ASSERT_EQ(listener1.StateChanges().size(), 3u);
  EXPECT_EQ(listener1.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener1.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(listener1.StateChanges()[2].state, fuchsia::ui::activity::State::IDLE);

  ASSERT_EQ(listener2.StateChanges().size(), 3u);
  EXPECT_EQ(listener2.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener2.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(listener2.StateChanges()[2].state, fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityAppTest, Control_OverrideState) {
  fuchsia::ui::activity::ProviderPtr provider1, provider2;
  app_->AddProviderBinding(provider1.NewRequest(dispatcher()));
  app_->AddProviderBinding(provider2.NewRequest(dispatcher()));
  fuchsia::ui::activity::ControlPtr control;
  app_->AddControlBinding(control.NewRequest(dispatcher()));

  testing::FakeListener listener1, listener2;
  provider1->WatchState(listener1.NewHandle(dispatcher()));
  provider2->WatchState(listener2.NewHandle(dispatcher()));
  RunLoopUntilIdle();

  control->SetState(fuchsia::ui::activity::State::ACTIVE);
  RunLoopUntilIdle();

  ASSERT_EQ(listener1.StateChanges().size(), 2u);
  EXPECT_EQ(listener1.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener1.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);

  ASSERT_EQ(listener2.StateChanges().size(), 2u);
  EXPECT_EQ(listener2.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener2.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);

  auto timeout = driver_->state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  // Timeouts do not trigger notification since the override state is set
  EXPECT_EQ(listener1.StateChanges().size(), 2u);
  EXPECT_EQ(listener2.StateChanges().size(), 2u);

  control->SetState(fuchsia::ui::activity::State::IDLE);
  RunLoopUntilIdle();

  ASSERT_EQ(listener1.StateChanges().size(), 3u);
  EXPECT_EQ(listener1.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener1.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(listener1.StateChanges()[2].state, fuchsia::ui::activity::State::IDLE);

  ASSERT_EQ(listener2.StateChanges().size(), 3u);
  EXPECT_EQ(listener2.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener2.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);
  EXPECT_EQ(listener2.StateChanges()[2].state, fuchsia::ui::activity::State::IDLE);
}

TEST_F(ActivityAppTest, Control_OverrideState_TrackerInputsNotSentToListeners) {
  fuchsia::ui::activity::ProviderPtr provider;
  app_->AddProviderBinding(provider.NewRequest(dispatcher()));
  fuchsia::ui::activity::ControlPtr control;
  app_->AddControlBinding(control.NewRequest(dispatcher()));
  fuchsia::ui::activity::TrackerPtr tracker;
  app_->AddTrackerBinding(tracker.NewRequest(dispatcher()));

  testing::FakeListener listener;
  provider->WatchState(listener.NewHandle(dispatcher()));
  RunLoopUntilIdle();

  control->SetState(fuchsia::ui::activity::State::ACTIVE);
  RunLoopUntilIdle();

  ASSERT_EQ(listener.StateChanges().size(), 2u);
  EXPECT_EQ(listener.StateChanges()[0].state, fuchsia::ui::activity::State::IDLE);
  EXPECT_EQ(listener.StateChanges()[1].state, fuchsia::ui::activity::State::ACTIVE);

  tracker->ReportDiscreteActivity(DiscreteActivity(), Now().get());
  RunLoopUntilIdle();

  // No additional transitions
  EXPECT_EQ(listener.StateChanges().size(), 2u);

  auto timeout = driver_->state_machine().TimeoutFor(fuchsia::ui::activity::State::ACTIVE);
  ASSERT_NE(timeout, std::nullopt);
  RunLoopFor(*timeout);

  // No additional transitions
  EXPECT_EQ(listener.StateChanges().size(), 2u);
}

}  // namespace

}  // namespace activity
