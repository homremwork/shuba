#pragma once

#include "Platform/AndroidPreviousExit.hpp"

namespace shuba::platform {
class JuceAndroidPreviousExitService final : public AndroidPreviousExitService {
public:
	[[nodiscard]] PlatformValueResult<AndroidPreviousExitInfo>
	query_previous_exit(
		const AndroidPreviousExitQueryRequest& request) override;
};
}	 // namespace shuba::platform
