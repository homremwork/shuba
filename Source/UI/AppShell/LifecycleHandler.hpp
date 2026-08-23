#pragma once

namespace shuba::ui {
class LifecycleHandler {
public:
	LifecycleHandler()								  = default;
	LifecycleHandler(const LifecycleHandler&) = delete;
	LifecycleHandler& operator=(const LifecycleHandler&) =
		delete;
	LifecycleHandler(LifecycleHandler&&)			= delete;
	LifecycleHandler& operator=(LifecycleHandler&&) = delete;
	virtual ~LifecycleHandler()								= default;

	virtual void handle_application_suspended() = 0;
	virtual void handle_application_resumed()	= 0;
};

class LifecycleDelegate final {
public:
	void set_handler(LifecycleHandler* handler_value) noexcept {
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
	LifecycleHandler* handler{};
};
}	 // namespace shuba::ui
