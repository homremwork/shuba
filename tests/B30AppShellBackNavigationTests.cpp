#include "UI/AppShell/BackHandler.hpp"
#include "UI/AppShell/BackNavigation.hpp"
#include "UI/AppShell/LifecycleHandler.hpp"
#include "UI/AppShell/RouteCoordinator.hpp"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <string>

namespace {
class FakeBackHandler final : public shuba::ui::BackHandler {
public:
	explicit FakeBackHandler(bool handled_value) : handled(handled_value) {}

	[[nodiscard]] bool handle_system_back() override {
		++calls;
		return handled;
	}

	bool handled{};
	std::uint32_t calls{};
};

class FakeLifecycleHandler final : public shuba::ui::LifecycleHandler {
public:
	void handle_application_suspended() override { ++suspended_calls; }
	void handle_application_resumed() override { ++resumed_calls; }

	std::uint32_t suspended_calls{};
	std::uint32_t resumed_calls{};
};

[[nodiscard]] shuba::core::StableIdentifier make_identifier(std::string text) {
	return *shuba::core::StableIdentifier::try_create(std::move(text));
}

[[nodiscard]] shuba::ui::BackNavigationState make_state(
	shuba::ui::RootDestination destination) {
	return shuba::ui::BackNavigationState{.destination = destination};
}

void require_action(const shuba::ui::BackDecision& decision,
					shuba::ui::BackAction action) {
	REQUIRE(decision.action == action);
}

struct RouteCoordinatorFixture final {
	shuba::ui::CatalogSessionState session;
	shuba::ui::RouteState route;
	shuba::ui::BackupState backup;
	shuba::ui::FeedbackState feedback;
	shuba::ui::PhotoDisplayState photo_display;
	shuba::ui::StorageDetailState storage_detail;
	std::uint32_t refresh_count{};
	std::uint32_t item_cleanup_count{};
	std::uint32_t storage_cleanup_count{};
	shuba::ui::RouteCoordinator coordinator{
		shuba::ui::RouteCoordinator::Dependencies{
			.session					 = session,
			.route						 = route,
			.backup						 = backup,
			.feedback					 = feedback,
			.photo_display				 = photo_display,
			.storage_detail				 = storage_detail,
			.cleanup_item_pending_photos = [this] { ++item_cleanup_count; },
			.cleanup_storage_pending_photos =
				[this] { ++storage_cleanup_count; },
			.refresh_all = [this] { ++refresh_count; }}};
};

[[nodiscard]] bool restore_contextual_location(
	RouteCoordinatorFixture& fixture) {
	const shuba::ui::BackDecision decision =
		shuba::ui::decide_back_navigation(
			shuba::ui::BackNavigationState{
				.destination = fixture.route.destination,
				.contextual_return_available =
					!fixture.route.contextual_return_locations.empty()});
	return fixture.coordinator.handle_system_back(decision);
}
}	 // namespace

TEST_CASE(
	"B30 Back policy preserves Android defaults only at Catalog and guarded "
	"states",
	"[b30][back-navigation]") {
	SECTION("active operation and fatal recovery are unhandled") {
		shuba::ui::BackNavigationState active =
			make_state(shuba::ui::RootDestination::ItemDetail);
		active.shell_operation_active = true;
		REQUIRE_FALSE(
			shuba::ui::decide_back_navigation(active).consumed());

		shuba::ui::BackNavigationState fatal =
			make_state(shuba::ui::RootDestination::BackupRecovery);
		fatal.session_fatal = true;
		REQUIRE_FALSE(
			shuba::ui::decide_back_navigation(fatal).consumed());
	}

	SECTION("Catalog is unhandled and other bottom roots select Catalog") {
		REQUIRE_FALSE(shuba::ui::decide_back_navigation(
						  make_state(shuba::ui::RootDestination::Catalog))
						  .consumed());
		for (const shuba::ui::RootDestination root :
			 {shuba::ui::RootDestination::Storages,
			  shuba::ui::RootDestination::Add,
			  shuba::ui::RootDestination::More}) {
			CAPTURE(static_cast<int>(root));
			require_action(
				shuba::ui::decide_back_navigation(make_state(root)),
				shuba::ui::BackAction::SelectCatalog);
		}
	}
}

TEST_CASE(
	"JI.9 application lifecycle delegate forwards only while a shell exists",
	"[ji9][b30][lifecycle]") {
	shuba::ui::LifecycleDelegate delegate;
	delegate.handle_application_suspended();
	delegate.handle_application_resumed();

	FakeLifecycleHandler handler;
	delegate.set_handler(&handler);
	delegate.handle_application_suspended();
	delegate.handle_application_resumed();
	delegate.handle_application_suspended();
	REQUIRE(handler.suspended_calls == 2U);
	REQUIRE(handler.resumed_calls == 1U);

	delegate.set_handler(nullptr);
	delegate.handle_application_suspended();
	delegate.handle_application_resumed();
	REQUIRE(handler.suspended_calls == 2U);
	REQUIRE(handler.resumed_calls == 1U);
}

TEST_CASE(
	"B30 Back policy preserves confirmation precedence and safe destination "
	"actions",
	"[b30][back-navigation]") {
	SECTION("catalog filter discards its draft before root fallback") {
		shuba::ui::BackNavigationState state =
			make_state(shuba::ui::RootDestination::Catalog);
		state.catalog_filter_panel_visible = true;
		require_action(shuba::ui::decide_back_navigation(state),
					   shuba::ui::BackAction::CloseCatalogFilterPanel);
	}

	SECTION("photo deletion confirmation cancels before viewer return") {
		shuba::ui::BackNavigationState state =
			make_state(shuba::ui::RootDestination::PhotoViewer);
		state.photo_deletion_confirmation_pending = true;
		state.selected_viewer_owner_is_item		  = true;
		require_action(shuba::ui::decide_back_navigation(state),
					   shuba::ui::BackAction::CancelPhotoDeletion);
	}

	SECTION(
		"detail restore, form return, viewer owner, and recovery actions are "
		"explicit") {
		shuba::ui::BackNavigationState detail =
			make_state(shuba::ui::RootDestination::ItemDetail);
		detail.contextual_return_available = true;
		require_action(
			shuba::ui::decide_back_navigation(detail),
			shuba::ui::BackAction::RestoreContextualLocation);
		detail.contextual_return_available = false;
		REQUIRE_FALSE(
			shuba::ui::decide_back_navigation(detail).consumed());

		require_action(shuba::ui::decide_back_navigation(
						   make_state(shuba::ui::RootDestination::ItemForm)),
					   shuba::ui::BackAction::ReturnToFormDestination);

		shuba::ui::BackNavigationState viewer =
			make_state(shuba::ui::RootDestination::PhotoViewer);
		viewer.selected_viewer_owner_is_storage = true;
		require_action(shuba::ui::decide_back_navigation(viewer),
					   shuba::ui::BackAction::ReturnPhotoViewerToOwner);

		shuba::ui::BackNavigationState recovery =
			make_state(shuba::ui::RootDestination::BackupRecovery);
		require_action(
			shuba::ui::decide_back_navigation(recovery),
			shuba::ui::BackAction::ReturnBackupRecoveryToMore);
		recovery.staged_import_confirmation_pending = true;
		REQUIRE_FALSE(
			shuba::ui::decide_back_navigation(recovery).consumed());
	}
}

TEST_CASE(
	"B30 contextual detail chain restores exact locations and clears on tab "
	"selection",
	"[b30][back-navigation][route-context]") {
	RouteCoordinatorFixture fixture;
	const shuba::core::StableIdentifier storage_one =
		make_identifier("storage-one");
	const shuba::core::StableIdentifier storage_two =
		make_identifier("storage-two");
	const shuba::core::StableIdentifier item_one = make_identifier("item-one");

	fixture.coordinator.open_storage_detail(storage_one);
	fixture.coordinator.open_storage_detail(storage_two);
	fixture.coordinator.open_item_detail(item_one);
	REQUIRE(fixture.route.contextual_return_locations.size() == 3U);

	const std::uint32_t refresh_count_before_back = fixture.refresh_count;
	REQUIRE(restore_contextual_location(fixture));
	REQUIRE(fixture.refresh_count == refresh_count_before_back + 1U);
	REQUIRE(fixture.item_cleanup_count == 0U);
	REQUIRE(fixture.storage_cleanup_count == 0U);
	REQUIRE(fixture.route.destination
			== shuba::ui::RootDestination::StorageDetail);
	REQUIRE(fixture.route.selected_storage_id == storage_two);
	REQUIRE(restore_contextual_location(fixture));
	REQUIRE(fixture.route.destination
			== shuba::ui::RootDestination::StorageDetail);
	REQUIRE(fixture.route.selected_storage_id == storage_one);
	REQUIRE(restore_contextual_location(fixture));
	REQUIRE(fixture.route.destination == shuba::ui::RootDestination::Catalog);
	REQUIRE_FALSE(fixture.route.selected_item_id.has_value());
	REQUIRE_FALSE(fixture.route.selected_storage_id.has_value());

	fixture.coordinator.open_storage_detail(storage_one);
	fixture.coordinator.select_root(shuba::ui::RootDestination::More);
	REQUIRE(fixture.route.contextual_return_locations.empty());
	require_action(shuba::ui::decide_back_navigation(
					   make_state(shuba::ui::RootDestination::More)),
				   shuba::ui::BackAction::SelectCatalog);
}

TEST_CASE(
	"B30 viewer and form Back paths preserve their established owners and "
	"cleanup",
	"[b30][back-navigation][route-context]") {
	RouteCoordinatorFixture fixture;
	const shuba::core::StableIdentifier storage_id =
		make_identifier("viewer-storage");
	fixture.coordinator.open_storage_detail(storage_id);
	fixture.coordinator.open_photo_viewer(
		shuba::domain::PhotoOwner{
			.type = shuba::domain::PhotoOwnerType::Storage, .id = storage_id},
		std::nullopt);

	REQUIRE(
		fixture.coordinator.handle_system_back(shuba::ui::BackDecision{
			.action =
				shuba::ui::BackAction::ReturnPhotoViewerToOwner}));
	REQUIRE(fixture.route.destination
			== shuba::ui::RootDestination::StorageDetail);
	REQUIRE(fixture.route.selected_storage_id == storage_id);
	REQUIRE(fixture.route.contextual_return_locations.size() == 1U);
	REQUIRE(restore_contextual_location(fixture));
	REQUIRE(fixture.route.destination == shuba::ui::RootDestination::Catalog);

	fixture.route.destination		  = shuba::ui::RootDestination::ItemForm;
	fixture.route.selected_storage_id = storage_id;
	fixture.route.form_return_destination =
		shuba::ui::RootDestination::StorageDetail;
	fixture.coordinator.return_from_form(
		shuba::ui::RootDestination::StorageDetail);
	REQUIRE(fixture.item_cleanup_count == 1U);
	REQUIRE(fixture.route.destination
			== shuba::ui::RootDestination::StorageDetail);
	REQUIRE(fixture.route.selected_storage_id == storage_id);

	fixture.route.destination = shuba::ui::RootDestination::StorageForm;
	fixture.route.form_return_destination = shuba::ui::RootDestination::Add;
	fixture.coordinator.return_from_form(shuba::ui::RootDestination::Add);
	REQUIRE(fixture.storage_cleanup_count == 1U);
	REQUIRE(fixture.route.destination == shuba::ui::RootDestination::Add);
}

TEST_CASE("B30 contextual chain retains its root anchor at the bounded maximum",
		  "[b30][back-navigation][route-context]") {
	RouteCoordinatorFixture fixture;
	for (std::size_t index = 0U;
		 index
		 < shuba::ui::RouteState::maximum_contextual_return_locations
			   + 32U;
		 ++index) {
		fixture.coordinator.open_storage_detail(
			make_identifier("storage-" + std::to_string(index)));
	}

	REQUIRE(
		fixture.route.contextual_return_locations.size()
		== shuba::ui::RouteState::maximum_contextual_return_locations);
	REQUIRE(fixture.route.contextual_return_locations.front().destination
			== shuba::ui::RootDestination::Catalog);
	while (!fixture.route.contextual_return_locations.empty())
		REQUIRE(restore_contextual_location(fixture));
	REQUIRE(fixture.route.destination == shuba::ui::RootDestination::Catalog);
}

TEST_CASE("B30 application back delegate preserves handler consumption",
		  "[b30][back-navigation][delegate]") {
	shuba::ui::BackDelegate delegate;
	FakeBackHandler handled{true};
	FakeBackHandler unhandled{false};

	REQUIRE_FALSE(delegate.handle_system_back());
	delegate.set_handler(&handled);
	REQUIRE(delegate.handle_system_back());
	REQUIRE(handled.calls == 1U);
	delegate.set_handler(&unhandled);
	REQUIRE_FALSE(delegate.handle_system_back());
	REQUIRE(unhandled.calls == 1U);
}
