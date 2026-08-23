#pragma once

namespace shuba::ui {
class AppShellLifecycleHandler {
public:
	AppShellLifecycleHandler()								  = default;
	AppShellLifecycleHandler(const AppShellLifecycleHandler&) = delete;
	AppShellLifecycleHandler& operator=(const AppShellLifecycleHandler&) =
		delete;
	AppShellLifecycleHandler(AppShellLifecycleHandler&&)			= delete;
	AppShellLifecycleHandler& operator=(AppShellLifecycleHandler&&) = delete;
	virtual ~AppShellLifecycleHandler()								= default;

	virtual void handle_application_suspended() = 0;
	virtual void handle_application_resumed()	= 0;
};

class AppShellLifecycleDelegate final {
public:
	void set_handler(AppShellLifecycleHandler* handler_value) noexcept {
		handler = handler_value;
	}

	void handle_application_suspended() const {
		if (handler != nullptr)
			handler->handle_application_suspended();
	}

	void handle_application_resumed() const {
		if (handler != nullptr)
			handler->handle_application_resumed();
	}

private:
	AppShellLifecycleHandler* handler{};
};
}	 // namespace shuba::ui
