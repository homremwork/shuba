#pragma once

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace shuba::ui {
class CallbackLifetimeToken final {
public:
	CallbackLifetimeToken()										   = default;
	CallbackLifetimeToken(const CallbackLifetimeToken&)			   = delete;
	CallbackLifetimeToken& operator=(const CallbackLifetimeToken&) = delete;
	CallbackLifetimeToken(CallbackLifetimeToken&&)				   = delete;
	CallbackLifetimeToken& operator=(CallbackLifetimeToken&&)	   = delete;

	void invalidate_and_wait() noexcept {
		alive.store(false, std::memory_order_release);
		std::unique_lock<std::mutex> lock{mutex};
		callbacks_finished.wait(lock, [this] {
			return callback_count.load(std::memory_order_acquire) == 0U;
		});
	}

private:
	friend class CallbackLifetimeLease;

	std::atomic_bool alive{true};
	std::atomic_uint32_t callback_count{};
	std::mutex mutex;
	std::condition_variable callbacks_finished;
};

class CallbackLifetimeLease final {
public:
	[[nodiscard]] static std::optional<CallbackLifetimeLease> try_acquire(
		const std::weak_ptr<CallbackLifetimeToken>& lifetime) {
		const std::shared_ptr<CallbackLifetimeToken> token = lifetime.lock();
		if (token == nullptr || !token->alive.load(std::memory_order_acquire))
			return std::nullopt;

		token->callback_count.fetch_add(1U, std::memory_order_acq_rel);
		if (!token->alive.load(std::memory_order_acquire)) {
			complete_callback(*token);
			return std::nullopt;
		}
		return CallbackLifetimeLease{token};
	}

	CallbackLifetimeLease(const CallbackLifetimeLease&)			   = delete;
	CallbackLifetimeLease& operator=(const CallbackLifetimeLease&) = delete;
	CallbackLifetimeLease(CallbackLifetimeLease&&) noexcept		   = default;
	CallbackLifetimeLease& operator=(CallbackLifetimeLease&&)	   = delete;

	~CallbackLifetimeLease() {
		if (token != nullptr)
			complete_callback(*token);
	}

private:
	explicit CallbackLifetimeLease(
		std::shared_ptr<CallbackLifetimeToken> token_value)
		: token(std::move(token_value)) {}

	static void complete_callback(CallbackLifetimeToken& token) noexcept {
		if (token.callback_count.fetch_sub(1U, std::memory_order_acq_rel) == 1U)
			token.callbacks_finished.notify_all();
	}

	std::shared_ptr<CallbackLifetimeToken> token;
};
}	 // namespace shuba::ui
