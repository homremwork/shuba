#pragma once

#include "Core/Identifier.hpp"

#include <mutex>
#include <optional>
#include <string_view>

namespace shuba::core {
enum class OperationKind {
	MetadataWrite,
	PhotoImport,
	BackupExport,
	BackupImport,
	CatalogReplacement,
	RecoveryCleanup,
};

[[nodiscard]] std::string_view to_string(OperationKind Kind) noexcept;

struct ActiveOperation final {
	OperationKind kind{OperationKind::MetadataWrite};
	OperationIdentifier operation_id;
};

class OperationGate final {
public:
	class Lease final {
	public:
		Lease(const Lease&)			   = delete;
		Lease& operator=(const Lease&) = delete;

		Lease(Lease&& Other) noexcept;
		Lease& operator=(Lease&& Other) noexcept;
		~Lease();

		[[nodiscard]] OperationKind kind() const noexcept;
		[[nodiscard]] const OperationIdentifier& operation_identifier() const;

		void release() noexcept;

	private:
		friend class OperationGate;

		Lease(OperationGate& Gate, OperationKind Kind,
			  OperationIdentifier OperationId);

		OperationGate* gate{};
		OperationKind operation_kind{OperationKind::MetadataWrite};
		std::optional<OperationIdentifier> leased_operation_identifier;
	};

	[[nodiscard]] std::optional<Lease> try_acquire(
		OperationKind Kind, OperationIdentifier OperationId);
	[[nodiscard]] bool is_busy() const;
	[[nodiscard]] std::optional<ActiveOperation> active_operation() const;

private:
	void release(const OperationIdentifier& OperationId) noexcept;

	mutable std::mutex mutex;
	std::optional<ActiveOperation> active_operation_value;
};
}	 // namespace shuba::core
