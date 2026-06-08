#include "Core/Clock.hpp"
#include "Core/Identifier.hpp"
#include "Core/OperationGate.hpp"
#include "Core/Result.hpp"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <stdexcept>

TEST_CASE("B03 identifier wrappers validate stable and file-safe text",
		  "[b03][identifiers]") {
	using shuba::core::IdentifierValidationIssue;
	using shuba::core::StableIdentifier;
	using shuba::core::validate_file_safe_identifier_text;
	using shuba::core::validate_lowercase_uuid_text;
	using shuba::core::validate_stable_identifier_text;

	REQUIRE(validate_stable_identifier_text("item-001"));
	REQUIRE(validate_stable_identifier_text("legacy id with spaces"));
	REQUIRE(validate_stable_identifier_text("").issue
			== IdentifierValidationIssue::Empty);
	REQUIRE(validate_stable_identifier_text("bad/path").issue
			== IdentifierValidationIssue::ContainsPathSeparator);

	REQUIRE(validate_file_safe_identifier_text("item-001"));
	REQUIRE(validate_file_safe_identifier_text("legacy id with spaces").issue
			== IdentifierValidationIssue::ContainsUnsafeFileCharacter);

	const auto identifier = StableIdentifier::try_create("item-001");
	REQUIRE(identifier.has_value());
	REQUIRE(identifier->value() == "item-001");
	REQUIRE_FALSE(StableIdentifier::try_create("bad/path").has_value());
	REQUIRE_FALSE(
		StableIdentifier::try_create_file_safe("Item-001").has_value());

	REQUIRE(
		validate_lowercase_uuid_text("00000000-0000-4000-8000-000000000000"));
	REQUIRE(validate_lowercase_uuid_text("00000000-0000-4000-8000-00000000000Z")
				.issue
			== IdentifierValidationIssue::InvalidLowercaseUuidShape);
}

TEST_CASE("B03 random identifier source can be deterministic for tests",
		  "[b03][identifiers]") {
	using shuba::core::RandomIdentifierSource;
	using shuba::core::validate_file_safe_identifier_text;
	using shuba::core::validate_lowercase_uuid_text;

	RandomIdentifierSource first{1234};
	RandomIdentifierSource second{1234};

	const auto first_entity	 = first.next_stable_identifier();
	const auto second_entity = second.next_stable_identifier();
	REQUIRE(first_entity == second_entity);
	REQUIRE(validate_lowercase_uuid_text(first_entity.value()));
	REQUIRE(validate_file_safe_identifier_text(first_entity.value()));

	const auto operation = first.next_operation_identifier();
	REQUIRE(validate_lowercase_uuid_text(operation.value()));
	REQUIRE(validate_file_safe_identifier_text(operation.value()));
	REQUIRE(operation.value() != first_entity.value());
}

TEST_CASE("B03 manual clock returns UTC epoch milliseconds deterministically",
		  "[b03][clock]") {
	using namespace std::chrono_literals;
	using shuba::core::Clock;
	using shuba::core::ManualClock;

	ManualClock clock{123ms};
	const Clock& clock_view = clock;

	REQUIRE(clock_view.now() == 123ms);

	clock.advance_by(7ms);
	REQUIRE(clock_view.now() == 130ms);

	clock.set_now(999ms);
	REQUIRE(clock_view.now() == 999ms);
}

TEST_CASE("B03 operation results distinguish success cancellation and failures",
		  "[b03][results]") {
	using shuba::core::Diagnostic;
	using shuba::core::DiagnosticSeverity;
	using shuba::core::OperationResult;
	using shuba::core::OperationResultCategory;
	using shuba::core::to_string;

	const auto success = OperationResult::success();
	REQUIRE(success.succeeded());
	REQUIRE_FALSE(success.failed());
	REQUIRE(success.category() == OperationResultCategory::Success);
	REQUIRE(to_string(success.category()) == "success");

	const auto cancelled = OperationResult::user_cancelled();
	REQUIRE(cancelled.was_user_cancelled());
	REQUIRE_FALSE(cancelled.failed());

	auto failure = OperationResult::failure(
		OperationResultCategory::ValidationFailure,
		Diagnostic{
			.severity		   = DiagnosticSeverity::ActionValidationError,
			.code			   = "storage-cycle",
			.message		   = "Storage parent chain cannot contain a cycle.",
			.technical_details = "storage id storage-001 references itself"});

	REQUIRE(failure.failed());
	REQUIRE_FALSE(failure.succeeded());
	REQUIRE(failure.diagnostics().size() == 1);
	REQUIRE(failure.diagnostics().front().severity
			== DiagnosticSeverity::ActionValidationError);
	REQUIRE(to_string(failure.diagnostics().front().severity)
			== "action validation error");

	failure.add_diagnostic(
		Diagnostic{.severity		  = DiagnosticSeverity::RecoverableWarning,
				   .code			  = "unknown-field",
				   .message			  = "Unknown field was preserved.",
				   .technical_details = "field extra"});
	REQUIRE(failure.diagnostics().size() == 2);

	REQUIRE_THROWS_AS(
		OperationResult::failure(
			OperationResultCategory::Success,
			Diagnostic{.severity = DiagnosticSeverity::WriteBlockingError,
					   .code	 = "invalid",
					   .message	 = "invalid",
					   .technical_details = "invalid"}),
		std::invalid_argument);
}

TEST_CASE("B03 operation gate serializes exclusive operations",
		  "[b03][operation-gate]") {
	using shuba::core::OperationGate;
	using shuba::core::OperationIdentifier;
	using shuba::core::OperationKind;
	using shuba::core::to_string;

	OperationGate gate;
	const auto first_operation =
		OperationIdentifier::try_create_file_safe("operation-001");
	const auto second_operation =
		OperationIdentifier::try_create_file_safe("operation-002");
	REQUIRE(first_operation.has_value());
	REQUIRE(second_operation.has_value());

	{
		auto first_lease =
			gate.try_acquire(OperationKind::MetadataWrite, *first_operation);
		REQUIRE(first_lease.has_value());
		REQUIRE(gate.is_busy());
		REQUIRE(first_lease->kind() == OperationKind::MetadataWrite);
		REQUIRE(first_lease->operation_identifier() == *first_operation);
		REQUIRE(to_string(first_lease->kind()) == "metadata write");

		const auto active = gate.active_operation();
		REQUIRE(active.has_value());
		REQUIRE(active->kind == OperationKind::MetadataWrite);
		REQUIRE(active->operation_id == *first_operation);

		auto blocked_lease =
			gate.try_acquire(OperationKind::BackupExport, *second_operation);
		REQUIRE_FALSE(blocked_lease.has_value());

		first_lease->release();
		REQUIRE_FALSE(gate.is_busy());
	}

	REQUIRE_FALSE(gate.is_busy());

	{
		auto lease =
			gate.try_acquire(OperationKind::BackupImport, *second_operation);
		REQUIRE(lease.has_value());
		REQUIRE(gate.is_busy());
	}

	REQUIRE_FALSE(gate.is_busy());
}
