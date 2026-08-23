#pragma once

namespace shuba::ui {
class BackHandler {
public:
	BackHandler()									   = default;
	BackHandler(const BackHandler&)			   = delete;
	BackHandler& operator=(const BackHandler&) = delete;
	BackHandler(BackHandler&&)				   = delete;
	BackHandler& operator=(BackHandler&&)	   = delete;
	virtual ~BackHandler()							   = default;

	[[nodiscard]] virtual bool handle_system_back() = 0;
};

class BackDelegate final {
public:
	void set_handler(BackHandler* handler_value) noexcept {
		handler = handler_value;
	}

	[[nodiscard]] bool handle_system_back() {
		return handler != nullptr && handler->handle_system_back();
	}

private:
	BackHandler* handler{};
};
}	 // namespace shuba::ui
