#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Localization/EmbeddedCatalog.hpp"
#include "Localization/Facade.hpp"
#include "Localization/Language.hpp"
#include "Platform/JpegXlPhotoCodec.hpp"
#include "Platform/JuceAndroidPreviousExit.hpp"
#include "Platform/JuceAndroidServices.hpp"
#include "UI/AppShell/Component.hpp"
#include "UI/Session/CatalogStartupSession.hpp"
#include "UI/Session/StartupRecoverySession.hpp"

#include "JuceHeader.h"

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace {
[[nodiscard]] std::string_view application_version() noexcept {
	return ProjectInfo::versionString;
}

[[nodiscard]] bool debug_demo_seed_enabled() noexcept {
#if defined(NDEBUG)
	return false;
#else
	return true;
#endif
}

[[nodiscard]] std::string_view platform_name() noexcept {
#if JUCE_ANDROID
	return "android";
#elif JUCE_IOS
	return "ios";
#elif JUCE_MAC
	return "macos";
#elif JUCE_WINDOWS
	return "windows";
#elif JUCE_LINUX
	return "linux";
#else
	return "unknown";
#endif
}

[[nodiscard]] shuba::ui::CatalogSessionState make_catalog_session(
	shuba::platform::AppPrivatePathProvider& path_provider,
	shuba::platform::AndroidPreviousExitService& android_previous_exit_service,
	shuba::core::IdentifierSource& identifiers, shuba::core::Clock& clock) {
	return shuba::ui::load_guarded_catalog_session(
		shuba::ui::GuardedCatalogSessionLoadRequest{
			.path_provider				   = path_provider,
			.identifiers				   = identifiers,
			.clock						   = clock,
			.app_version				   = std::string{application_version()},
			.platform					   = std::string{platform_name()},
			.debug_demo_seed_enabled	   = debug_demo_seed_enabled(),
			.android_previous_exit_service = &android_previous_exit_service});
}

[[nodiscard]] shuba::ui::Component::PlatformServices shell_services(
	shuba::platform::InternalPhotoCodec& internal_photo_codec,
	shuba::platform::AppPrivatePathProvider& path_provider,
	shuba::platform::AndroidPreviousExitService& android_previous_exit_service,
	shuba::localization::Localization& localization) {
	return shuba::ui::Component::PlatformServices{
		.internal_photo_codec		   = internal_photo_codec,
		.path_provider				   = path_provider,
		.android_previous_exit_service = android_previous_exit_service,
		.localization				   = localization,
		.app_version				   = std::string{application_version()},
		.platform_name				   = std::string{platform_name()},
		.debug_demo_seed_enabled	   = debug_demo_seed_enabled()};
}

void update_ui_construction_stage(
	const std::optional<shuba::platform::AppPrivatePaths>& paths,
	shuba::ui::CatalogSessionStartupSource source) {
	if (!paths.has_value()
		|| source
			   == shuba::ui::CatalogSessionStartupSource::
				   StartupCrashSafeMode) {
		return;
	}

	static_cast<void>(
		shuba::ui::update_startup_attempt_stage(*paths, "ui-construction"));
}

[[nodiscard]] shuba::ui::CatalogSessionState
make_ui_construction_exception_session(shuba::platform::AppPrivatePaths paths,
									   const shuba::core::Clock& clock,
									   std::string exception_kind,
									   std::string message,
									   std::string technical_details) {
	return shuba::ui::make_startup_exception_session(
		shuba::ui::StartupExceptionSessionRequest{
			.paths			   = std::move(paths),
			.captured_at	   = clock.now(),
			.app_version	   = std::string{application_version()},
			.platform		   = std::string{platform_name()},
			.fallback_stage	   = "ui-construction",
			.exception_kind	   = std::move(exception_kind),
			.message		   = std::move(message),
			.technical_details = std::move(technical_details)});
}

class MainWindow final : public juce::DocumentWindow {
public:
	MainWindow(juce::String name,
			   shuba::localization::Localization localization)
		: DocumentWindow(std::move(name), juce::Colours::black,
						 DocumentWindow::allButtons)
		, internal_photo_codec(
			  std::make_unique<shuba::platform::JpegXlInternalPhotoCodec>())
		, localization_service(std::move(localization)) {
		setOpaque(true);
		setUsingNativeTitleBar(true);
		shuba::core::RandomIdentifierSource identifiers;
		shuba::core::SystemClock clock;
		shuba::ui::CatalogSessionState startup_session = make_catalog_session(
			path_provider, android_previous_exit_service, identifiers, clock);
		std::optional<shuba::platform::AppPrivatePaths> startup_paths =
			startup_session.paths;
		update_ui_construction_stage(startup_paths, startup_session.source);
		try {
			setContentOwned(
				new shuba::ui::Component(
					std::move(startup_session),
					shell_services(*internal_photo_codec, path_provider,
								   android_previous_exit_service,
								   localization_service)),
				true);
		} catch (const std::exception& exception) {
			if (!startup_paths.has_value())
				throw;
			shuba::ui::CatalogSessionState exception_session =
				make_ui_construction_exception_session(
					std::move(*startup_paths), clock, "std::exception",
					exception.what(),
					"App shell construction threw before the initial UI could "
					"be "
					"shown.");
			setContentOwned(
				new shuba::ui::Component(
					std::move(exception_session),
					shell_services(*internal_photo_codec, path_provider,
								   android_previous_exit_service,
								   localization_service)),
				true);
		} catch (...) {
			if (!startup_paths.has_value())
				throw;
			shuba::ui::CatalogSessionState exception_session =
				make_ui_construction_exception_session(
					std::move(*startup_paths), clock, "unknown",
					"unknown UI construction exception",
					"App shell construction threw a non-standard exception "
					"before "
					"the initial UI could be shown.");
			setContentOwned(
				new shuba::ui::Component(
					std::move(exception_session),
					shell_services(*internal_photo_codec, path_provider,
								   android_previous_exit_service,
								   localization_service)),
				true);
		}
		back_delegate.set_handler(dynamic_cast<shuba::ui::BackHandler*>(
			getContentComponent()));
		lifecycle_delegate.set_handler(
			dynamic_cast<shuba::ui::LifecycleHandler*>(
				getContentComponent()));
#if JUCE_ANDROID
		setFullScreen(true);
#else
		centreWithSize(getWidth(), getHeight());
#endif
		setVisible(true);
	}

	~MainWindow() override {
		lifecycle_delegate.set_handler(nullptr);
		back_delegate.set_handler(nullptr);
		clearContentComponent();
	}

	[[nodiscard]] bool handle_system_back() {
		return back_delegate.handle_system_back();
	}

	void handle_application_suspended() {
		lifecycle_delegate.handle_application_suspended();
	}

	void handle_application_resumed() {
		lifecycle_delegate.handle_application_resumed();
	}

	void closeButtonPressed() override {
		juce::JUCEApplication::getInstance()->systemRequestedQuit();
	}

#if JUCE_IOS || JUCE_ANDROID
	void parentSizeChanged() override {
		juce::Component* content_component = getContentComponent();
		if (content_component != nullptr)
			content_component->resized();
	}
#endif

private:
	std::unique_ptr<shuba::platform::JpegXlInternalPhotoCodec>
		internal_photo_codec;
	shuba::platform::JuceAndroidPathProvider path_provider;
	shuba::platform::JuceAndroidPreviousExitService
		android_previous_exit_service;
	shuba::localization::Localization localization_service;
	shuba::ui::BackDelegate back_delegate;
	shuba::ui::LifecycleDelegate lifecycle_delegate;
};

class ShubaApplication final : public juce::JUCEApplication {
public:
	const juce::String getApplicationName() override { return "Shuba"; }

	const juce::String getApplicationVersion() override {
		return juce::String::fromUTF8(
			application_version().data(),
			static_cast<int>(application_version().size()));
	}

	bool moreThanOneInstanceAllowed() override { return false; }

	void initialise(const juce::String&) override {
		const juce::String user_language = juce::SystemStats::getUserLanguage();
		const std::string user_language_utf8 = user_language.toStdString();
		const shuba::localization::Language language =
			shuba::localization::resolve_language(user_language_utf8);
		shuba::localization::Localization localization =
			shuba::localization::make_localization(
				language, shuba::localization::embedded_russian_catalog());
		main_window = std::make_unique<MainWindow>(getApplicationName(),
												   std::move(localization));
	}

	void shutdown() override { main_window = nullptr; }

	void systemRequestedQuit() override { quit(); }

	bool backButtonPressed() override {
		return main_window != nullptr && main_window->handle_system_back();
	}

	void suspended() override {
		if (main_window != nullptr)
			main_window->handle_application_suspended();
	}

	void resumed() override {
		if (main_window != nullptr)
			main_window->handle_application_resumed();
	}

	void unhandledException(const std::exception* exception,
							const juce::String& source_filename,
							int line_number) override {
		juce::ignoreUnused(source_filename, line_number);
		try {
			shuba::platform::JuceAndroidPathProvider path_provider;
			shuba::platform::PlatformValueResult<
				shuba::platform::AppPrivatePaths>
				paths = path_provider.resolve_app_private_paths();
			if (!paths.succeeded())
				return;

			shuba::core::SystemClock clock;
			shuba::ui::StartupExceptionReport report{
				.captured_at = clock.now(),
				.app_version = std::string{application_version()},
				.platform	 = std::string{platform_name()},
				.stage		 = "post-startup",
				.exception_kind =
					exception == nullptr ? "unknown" : "std::exception",
				.message = exception == nullptr
							   ? "unknown post-startup exception"
							   : exception->what(),
				.technical_details =
					"JUCEApplication::unhandledException captured a "
					"post-startup exception locally."};
			static_cast<void>(shuba::ui::write_startup_exception_report(
				*paths.value, report));
		} catch (...) {}
	}

private:
	std::unique_ptr<MainWindow> main_window;
};
}	 // namespace

START_JUCE_APPLICATION(ShubaApplication)
