#pragma once

namespace shuba::ui {
class AppShellBackHandler {
public:
	AppShellBackHandler()									   = default;
	AppShellBackHandler(const AppShellBackHandler&)			   = delete;
	AppShellBackHandler& operator=(const AppShellBackHandler&) = delete;
	AppShellBackHandler(AppShellBackHandler&&)				   = delete;
	AppShellBackHandler& operator=(AppShellBackHandler&&)	   = delete;
	virtual ~AppShellBackHandler()							   = default;

	[[nodiscard]] virtual bool handle_system_back() = 0;
};

class AppShellBackDelegate final {
public:
	void set_handler(AppShellBackHandler* handler_value) noexcept {
		handler = handler_value;
	}

	[[nodiscard]] bool handle_system_back() {
		return handler != nullptr && handler->handle_system_back();
	}

private:
	AppShellBackHandler* handler{};
};
}	 // namespace shuba::ui
