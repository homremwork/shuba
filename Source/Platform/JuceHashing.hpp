#pragma once

#include "Platform/PlatformServices.hpp"

namespace shuba::platform {
class JuceMd5SourceByteFingerprintService final
	: public SourceByteFingerprintService {
public:
	[[nodiscard]] PlatformValueResult<SourceByteFingerprint>
	fingerprint_source_bytes(const SourceByteFingerprintRequest& request,
							 const PlatformOperationContext& context,
							 ProgressSink& progress_sink,
							 CancellationToken& cancellation_token) override;
};
}	 // namespace shuba::platform
