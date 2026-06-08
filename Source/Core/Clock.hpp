#pragma once

#include <chrono>

namespace shuba::core {
using EpochMilliseconds = std::chrono::milliseconds;

class Clock {
public:
	Clock()							   = default;
	Clock(const Clock&)				   = default;
	Clock& operator=(const Clock&)	   = default;
	Clock(Clock&&) noexcept			   = default;
	Clock& operator=(Clock&&) noexcept = default;
	virtual ~Clock()				   = default;

	[[nodiscard]] virtual EpochMilliseconds now() const = 0;
};

class SystemClock final : public Clock {
public:
	[[nodiscard]] EpochMilliseconds now() const override;
};

class ManualClock final : public Clock {
public:
	explicit ManualClock(EpochMilliseconds InitialNow = EpochMilliseconds{
							 0}) noexcept;

	[[nodiscard]] EpochMilliseconds now() const override;
	void set_now(EpochMilliseconds Value) noexcept;
	void advance_by(EpochMilliseconds Delta) noexcept;

private:
	EpochMilliseconds current_time{};
};
}	 // namespace shuba::core
