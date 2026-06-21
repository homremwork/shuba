#pragma once

#include "UI/Session/EntityEditTypes.hpp"

namespace shuba::ui {
[[nodiscard]] EntityEditResult save_item_draft(const EntityEditRequest& request,
											   const ItemDraft& draft);
[[nodiscard]] EntityEditResult save_storage_draft(
	const EntityEditRequest& request, const StorageDraft& draft);
[[nodiscard]] EntityEditResult archive_item_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& item_id);
[[nodiscard]] EntityEditResult archive_storage_in_session(
	const EntityEditRequest& request, const core::StableIdentifier& storage_id,
	bool archive_warning_acknowledged);
}	 // namespace shuba::ui
