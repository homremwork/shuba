#include "Core/OperationGate.hpp"

#include <mutex>
#include <utility>

namespace shuba::core {
std::string_view to_string(OperationKind Kind) noexcept {
	switch (Kind) {
		case OperationKind::MetadataWrite:
			return "metadata write";
		case OperationKind::PhotoImport:
			return "photo import";
		case OperationKind::JpegExport:
			return "JPEG export";
		case OperationKind::BackupExport:
			return "backup export";
		case OperationKind::BackupImport:
			return "backup import";
		case OperationKind::CatalogReplacement:
			return "catalog replacement";
		case OperationKind::RecoveryCleanup:
			return "recovery cleanup";
	}

	return "unknown operation kind";
}

OperationGate::Lease::Lease(Lease&& Other) noexcept
	: gate(std::exchange(Other.gate, nullptr))
	, operation_kind(Other.operation_kind)
	, leased_operation_identifier(
		  std::move(Other.leased_operation_identifier)) {}

OperationGate::Lease& OperationGate::Lease::operator=(Lease&& Other) noexcept {
	if (this != &Other) {
		release();
		gate		   = std::exchange(Other.gate, nullptr);
		operation_kind = Other.operation_kind;
		leased_operation_identifier =
			std::move(Other.leased_operation_identifier);
	}

	return *this;
}

OperationGate::Lease::~Lease() {
	release();
}

OperationKind OperationGate::Lease::kind() const noexcept {
	return operation_kind;
}

const OperationIdentifier& OperationGate::Lease::operation_identifier() const {
	return *leased_operation_identifier;
}

void OperationGate::Lease::release() noexcept {
	if (gate == nullptr || !leased_operation_identifier.has_value())
		return;

	gate->release(*leased_operation_identifier);
	gate = nullptr;
	leased_operation_identifier.reset();
}

OperationGate::Lease::Lease(OperationGate& Gate, OperationKind Kind,
							OperationIdentifier OperationId)
	: gate(&Gate)
	, operation_kind(Kind)
	, leased_operation_identifier(std::move(OperationId)) {}

std::optional<OperationGate::Lease> OperationGate::try_acquire(
	OperationKind Kind, OperationIdentifier OperationId) {
	const auto lock = std::lock_guard{mutex};

	if (active_operation_value.has_value())
		return std::nullopt;

	active_operation_value =
		ActiveOperation{.kind = Kind, .operation_id = OperationId};
	return Lease(*this, Kind, std::move(OperationId));
}

bool OperationGate::is_busy() const {
	const auto lock = std::lock_guard{mutex};
	return active_operation_value.has_value();
}

std::optional<ActiveOperation> OperationGate::active_operation() const {
	const auto lock = std::lock_guard{mutex};
	return active_operation_value;
}

void OperationGate::release(const OperationIdentifier& OperationId) noexcept {
	const auto lock = std::lock_guard{mutex};

	if (active_operation_value.has_value()
		&& active_operation_value->operation_id == OperationId) {
		active_operation_value.reset();
	}
}
}	 // namespace shuba::core
