#include "Core/Clock.hpp"

namespace shuba::core {
EpochMilliseconds SystemClock::now() const {
	return std::chrono::duration_cast<EpochMilliseconds>(
		std::chrono::system_clock::now().time_since_epoch());
}

ManualClock::ManualClock(EpochMilliseconds InitialNow) noexcept
	: current_time(InitialNow) {}

EpochMilliseconds ManualClock::now() const {
	return current_time;
}

void ManualClock::set_now(EpochMilliseconds Value) noexcept {
	current_time = Value;
}

void ManualClock::advance_by(EpochMilliseconds Delta) noexcept {
	current_time += Delta;
}
}	 // namespace shuba::core
