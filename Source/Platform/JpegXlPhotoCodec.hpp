#pragma once

#include "Platform/PlatformServices.hpp"

namespace shuba::platform {
class JpegXlInternalPhotoCodec final : public InternalPhotoCodec {
public:
	[[nodiscard]] PlatformValueResult<MediaWriteResult> encode_internal_photo(
		const InternalPhotoEncodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
	[[nodiscard]] PlatformValueResult<ImagePixels> decode_internal_photo(
		const InternalPhotoDecodeRequest& request,
		const PlatformOperationContext& context, ProgressSink& progress_sink,
		CancellationToken& cancellation_token) override;
};
}	 // namespace shuba::platform
