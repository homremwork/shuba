#include "Platform/JuceAndroidPreviousExit.hpp"

#if JUCE_ANDROID && !defined(JUCE_CORE_INCLUDE_JNI_HELPERS)
#define JUCE_CORE_INCLUDE_JNI_HELPERS 1
#endif

#include <juce_gui_basics/juce_gui_basics.h>

#if JUCE_ANDROID
#include <jni.h>
#endif

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace shuba::platform {
namespace {
constexpr std::size_t trace_copy_buffer_size = 32768U;

[[nodiscard]] core::Diagnostic make_diagnostic(
	core::DiagnosticSeverity severity, std::string code, std::string message,
	std::string technical_details = {}) {
	return core::Diagnostic{.severity		   = severity,
							.code			   = std::move(code),
							.message		   = std::move(message),
							.technical_details = std::move(technical_details)};
}

void add_diagnostic(std::vector<core::Diagnostic>& diagnostics,
					std::string code, std::string message,
					std::string technical_details = {}) {
	diagnostics.push_back(make_diagnostic(
		core::DiagnosticSeverity::RecoverableWarning, std::move(code),
		std::move(message), std::move(technical_details)));
}

#if JUCE_ANDROID
bool clear_jni_exception(JNIEnv* env,
						 std::vector<core::Diagnostic>& diagnostics,
						 std::string code, std::string message) {
	if (env == nullptr || !env->ExceptionCheck())
		return false;

	env->ExceptionClear();
	add_diagnostic(diagnostics, std::move(code), std::move(message));
	return true;
}

[[nodiscard]] std::optional<std::string> jstring_text(
	JNIEnv* env, jstring value, std::vector<core::Diagnostic>& diagnostics) {
	if (env == nullptr || value == nullptr)
		return std::nullopt;

	const char* chars = env->GetStringUTFChars(value, nullptr);
	if (chars == nullptr) {
		clear_jni_exception(env, diagnostics,
							"android-previous-exit-string-read-failed",
							"Android previous-exit string could not be read.");
		return std::nullopt;
	}

	std::string text{chars};
	env->ReleaseStringUTFChars(value, chars);
	if (clear_jni_exception(
			env, diagnostics, "android-previous-exit-string-release-failed",
			"Android previous-exit string could not be released.")) {
		return std::nullopt;
	}

	return text;
}

[[nodiscard]] jmethodID method_id(JNIEnv* env, jclass type, const char* name,
								  const char* signature,
								  std::vector<core::Diagnostic>& diagnostics,
								  std::string code, std::string message) {
	if (env == nullptr || type == nullptr)
		return nullptr;

	jmethodID method = env->GetMethodID(type, name, signature);
	if (method == nullptr) {
		clear_jni_exception(env, diagnostics, std::move(code),
							std::move(message));
	}
	return method;
}

[[nodiscard]] juce::LocalRef<jclass> find_class(
	JNIEnv* env, const char* class_name,
	std::vector<core::Diagnostic>& diagnostics, std::string code,
	std::string message) {
	if (env == nullptr)
		return juce::LocalRef<jclass>{};

	juce::LocalRef<jclass> type{env->FindClass(class_name)};
	if (type == nullptr) {
		clear_jni_exception(env, diagnostics, std::move(code),
							std::move(message));
	}
	return type;
}

[[nodiscard]] AndroidPreviousExitInfo unavailable_info(
	const AndroidPreviousExitQueryRequest& request) {
	return AndroidPreviousExitInfo{
		.captured_at	 = request.captured_at,
		.reason_name	 = std::string{android_exit_reason_name(
			android_application_exit_reason_unknown)},
		.trace_requested = request.capture_trace,
		.trace_path		 = request.trace_output_path};
}

[[nodiscard]] juce::LocalRef<jobject> activity_manager(
	JNIEnv* env, std::vector<core::Diagnostic>& diagnostics) {
	juce::LocalRef<jobject> context = juce::getAppContext();
	if (context == nullptr) {
		add_diagnostic(
			diagnostics, "android-previous-exit-context-unavailable",
			"Android app context is unavailable for previous-exit query.");
		return juce::LocalRef<jobject>{};
	}

	juce::LocalRef<jclass> context_class = find_class(
		env, "android/content/Context", diagnostics,
		"android-previous-exit-context-class-unavailable",
		"Android Context class is unavailable for previous-exit query.");
	jmethodID get_system_service =
		method_id(env, context_class.get(), "getSystemService",
				  "(Ljava/lang/String;)Ljava/lang/Object;", diagnostics,
				  "android-previous-exit-service-method-unavailable",
				  "Android Context.getSystemService method is unavailable.");
	if (get_system_service == nullptr)
		return juce::LocalRef<jobject>{};

	juce::LocalRef<jstring> service_name{env->NewStringUTF("activity")};
	if (service_name == nullptr) {
		clear_jni_exception(
			env, diagnostics, "android-previous-exit-service-name-failed",
			"Android activity service name could not be allocated.");
		return juce::LocalRef<jobject>{};
	}

	juce::LocalRef<jobject> service{env->CallObjectMethod(
		context.get(), get_system_service, service_name.get())};
	if (clear_jni_exception(
			env, diagnostics, "android-previous-exit-service-query-failed",
			"Android activity service could not be resolved.")) {
		return juce::LocalRef<jobject>{};
	}
	if (service == nullptr) {
		add_diagnostic(
			diagnostics, "android-previous-exit-service-unavailable",
			"Android activity service is unavailable for previous-exit query.");
	}
	return service;
}

[[nodiscard]] std::optional<juce::LocalRef<jobject>> newest_exit_record(
	JNIEnv* env, jobject manager, std::vector<core::Diagnostic>& diagnostics) {
	juce::LocalRef<jclass> activity_manager_class =
		find_class(env, "android/app/ActivityManager", diagnostics,
				   "android-previous-exit-activity-manager-class-unavailable",
				   "Android ActivityManager class is unavailable.");
	jmethodID historical_reasons = method_id(
		env, activity_manager_class.get(), "getHistoricalProcessExitReasons",
		"(Ljava/lang/String;II)Ljava/util/List;", diagnostics,
		"android-previous-exit-query-method-unavailable",
		"Android ActivityManager previous-exit method is unavailable.");
	if (historical_reasons == nullptr)
		return std::nullopt;

	juce::LocalRef<jobject> records{
		env->CallObjectMethod(manager, historical_reasons, nullptr, 0, 1)};
	if (clear_jni_exception(
			env, diagnostics, "android-previous-exit-query-failed",
			"Android previous-exit records could not be queried.")) {
		return std::nullopt;
	}
	if (records == nullptr) {
		add_diagnostic(diagnostics, "android-previous-exit-records-unavailable",
					   "Android previous-exit record list is unavailable.");
		return std::nullopt;
	}

	juce::LocalRef<jclass> list_class =
		find_class(env, "java/util/List", diagnostics,
				   "android-previous-exit-list-class-unavailable",
				   "Java List class is unavailable for previous-exit records.");
	jmethodID size_method = method_id(
		env, list_class.get(), "size", "()I", diagnostics,
		"android-previous-exit-list-size-unavailable",
		"Java List.size method is unavailable for previous-exit records.");
	jmethodID get_method = method_id(
		env, list_class.get(), "get", "(I)Ljava/lang/Object;", diagnostics,
		"android-previous-exit-list-get-unavailable",
		"Java List.get method is unavailable for previous-exit records.");
	if (size_method == nullptr || get_method == nullptr)
		return std::nullopt;

	const jint record_count = env->CallIntMethod(records.get(), size_method);
	if (clear_jni_exception(
			env, diagnostics, "android-previous-exit-list-size-failed",
			"Android previous-exit record count could not be read.")) {
		return std::nullopt;
	}
	if (record_count <= 0) {
		add_diagnostic(diagnostics, "android-previous-exit-record-unavailable",
					   "Android did not return a previous-exit record.");
		return std::nullopt;
	}

	juce::LocalRef<jobject> record{
		env->CallObjectMethod(records.get(), get_method, 0)};
	if (clear_jni_exception(
			env, diagnostics, "android-previous-exit-record-read-failed",
			"Android previous-exit record could not be read.")) {
		return std::nullopt;
	}
	if (record == nullptr) {
		add_diagnostic(diagnostics, "android-previous-exit-record-empty",
					   "Android previous-exit record was empty.");
		return std::nullopt;
	}

	return std::optional<juce::LocalRef<jobject>>{std::move(record)};
}

void classify_exit_info(AndroidPreviousExitInfo& info) {
	info.reason_name = std::string{android_exit_reason_name(info.reason)};
	info.native_crash =
		info.reason == android_application_exit_reason_crash_native;
	info.java_crash = info.reason == android_application_exit_reason_crash;
	info.anr		= info.reason == android_application_exit_reason_anr;
	info.actionable_for_startup =
		info.native_crash || info.java_crash || info.anr;
}

void read_exit_record_fields(JNIEnv* env, jobject record,
							 AndroidPreviousExitInfo& info,
							 std::vector<core::Diagnostic>& diagnostics) {
	juce::LocalRef<jclass> record_class =
		find_class(env, "android/app/ApplicationExitInfo", diagnostics,
				   "android-previous-exit-info-class-unavailable",
				   "Android ApplicationExitInfo class is unavailable.");
	jmethodID get_timestamp =
		method_id(env, record_class.get(), "getTimestamp", "()J", diagnostics,
				  "android-previous-exit-timestamp-unavailable",
				  "Android previous-exit timestamp method is unavailable.");
	jmethodID get_pid =
		method_id(env, record_class.get(), "getPid", "()I", diagnostics,
				  "android-previous-exit-pid-unavailable",
				  "Android previous-exit pid method is unavailable.");
	jmethodID get_process_name = method_id(
		env, record_class.get(), "getProcessName", "()Ljava/lang/String;",
		diagnostics, "android-previous-exit-process-name-unavailable",
		"Android previous-exit process-name method is unavailable.");
	jmethodID get_reason =
		method_id(env, record_class.get(), "getReason", "()I", diagnostics,
				  "android-previous-exit-reason-unavailable",
				  "Android previous-exit reason method is unavailable.");
	jmethodID get_status =
		method_id(env, record_class.get(), "getStatus", "()I", diagnostics,
				  "android-previous-exit-status-unavailable",
				  "Android previous-exit status method is unavailable.");
	jmethodID get_importance =
		method_id(env, record_class.get(), "getImportance", "()I", diagnostics,
				  "android-previous-exit-importance-unavailable",
				  "Android previous-exit importance method is unavailable.");
	jmethodID get_description = method_id(
		env, record_class.get(), "getDescription", "()Ljava/lang/String;",
		diagnostics, "android-previous-exit-description-unavailable",
		"Android previous-exit description method is unavailable.");

	if (get_timestamp != nullptr) {
		const jlong timestamp = env->CallLongMethod(record, get_timestamp);
		if (!clear_jni_exception(
				env, diagnostics, "android-previous-exit-timestamp-read-failed",
				"Android previous-exit timestamp could not be read.")) {
			info.record_timestamp = core::EpochMilliseconds{timestamp};
		}
	}
	if (get_pid != nullptr) {
		info.process_id = env->CallIntMethod(record, get_pid);
		clear_jni_exception(
			env, diagnostics, "android-previous-exit-pid-read-failed",
			"Android previous-exit process id could not be read.");
	}
	if (get_process_name != nullptr) {
		juce::LocalRef<jstring> process_name{static_cast<jstring>(
			env->CallObjectMethod(record, get_process_name))};
		if (!clear_jni_exception(
				env, diagnostics,
				"android-previous-exit-process-name-read-failed",
				"Android previous-exit process name could not be read.")) {
			info.process_name =
				jstring_text(env, process_name.get(), diagnostics)
					.value_or(std::string{});
		}
	}
	if (get_reason != nullptr) {
		info.reason = env->CallIntMethod(record, get_reason);
		clear_jni_exception(env, diagnostics,
							"android-previous-exit-reason-read-failed",
							"Android previous-exit reason could not be read.");
	}
	if (get_status != nullptr) {
		info.status = env->CallIntMethod(record, get_status);
		clear_jni_exception(env, diagnostics,
							"android-previous-exit-status-read-failed",
							"Android previous-exit status could not be read.");
	}
	if (get_importance != nullptr) {
		info.importance = env->CallIntMethod(record, get_importance);
		clear_jni_exception(
			env, diagnostics, "android-previous-exit-importance-read-failed",
			"Android previous-exit importance could not be read.");
	}
	if (get_description != nullptr) {
		juce::LocalRef<jstring> description{static_cast<jstring>(
			env->CallObjectMethod(record, get_description))};
		if (!clear_jni_exception(
				env, diagnostics,
				"android-previous-exit-description-read-failed",
				"Android previous-exit description could not be read.")) {
			info.description = jstring_text(env, description.get(), diagnostics)
								   .value_or(std::string{});
		}
	}
	classify_exit_info(info);
}

void close_trace_stream(JNIEnv* env, jobject stream, jmethodID close_method,
						std::vector<core::Diagnostic>& diagnostics) {
	if (stream == nullptr || close_method == nullptr)
		return;
	env->CallVoidMethod(stream, close_method);
	clear_jni_exception(
		env, diagnostics, "android-previous-exit-trace-close-failed",
		"Android previous-exit trace stream could not be closed.");
}

void remove_partial_trace(const std::filesystem::path& path) {
	std::error_code ignored;
	std::filesystem::remove(path, ignored);
}

void copy_trace_stream(JNIEnv* env, jobject stream,
					   AndroidPreviousExitInfo& info,
					   std::vector<core::Diagnostic>& diagnostics) {
	juce::LocalRef<jclass> stream_class = find_class(
		env, "java/io/InputStream", diagnostics,
		"android-previous-exit-trace-stream-class-unavailable",
		"Java InputStream class is unavailable for previous-exit trace.");
	jmethodID read_method = method_id(
		env, stream_class.get(), "read", "([B)I", diagnostics,
		"android-previous-exit-trace-read-method-unavailable",
		"Java InputStream.read method is unavailable for previous-exit trace.");
	jmethodID close_method =
		method_id(env, stream_class.get(), "close", "()V", diagnostics,
				  "android-previous-exit-trace-close-method-unavailable",
				  "Java InputStream.close method is unavailable for "
				  "previous-exit trace.");
	if (read_method == nullptr || close_method == nullptr) {
		info.trace_error_text = "Trace stream methods are unavailable.";
		return;
	}

	std::error_code error;
	std::filesystem::create_directories(info.trace_path.parent_path(), error);
	if (error) {
		info.trace_error_text = error.message();
		add_diagnostic(
			diagnostics, "android-previous-exit-trace-directory-unavailable",
			"Android previous-exit trace directory could not be created.",
			error.message());
		return;
	}

	std::ofstream output{info.trace_path, std::ios::binary | std::ios::trunc};
	if (!output) {
		info.trace_error_text = info.trace_path.string();
		add_diagnostic(
			diagnostics, "android-previous-exit-trace-write-failed",
			"Android previous-exit trace could not be opened for writing.",
			info.trace_path.string());
		return;
	}

	juce::LocalRef<jbyteArray> java_buffer{
		env->NewByteArray(static_cast<jsize>(trace_copy_buffer_size))};
	if (java_buffer == nullptr) {
		clear_jni_exception(
			env, diagnostics, "android-previous-exit-trace-buffer-failed",
			"Android previous-exit trace buffer could not be allocated.");
		info.trace_error_text = "Trace byte buffer allocation failed.";
		return;
	}

	std::array<jbyte, trace_copy_buffer_size> native_buffer{};
	std::uint64_t copied{};
	bool failed{};
	while (true) {
		const jint bytes_read =
			env->CallIntMethod(stream, read_method, java_buffer.get());
		if (clear_jni_exception(
				env, diagnostics, "android-previous-exit-trace-read-failed",
				"Android previous-exit trace stream could not be read.")) {
			info.trace_error_text = "Trace stream read failed.";
			failed				  = true;
			break;
		}
		if (bytes_read < 0)
			break;
		if (bytes_read == 0)
			continue;

		env->GetByteArrayRegion(java_buffer.get(), 0, bytes_read,
								native_buffer.data());
		if (clear_jni_exception(
				env, diagnostics,
				"android-previous-exit-trace-buffer-read-failed",
				"Android previous-exit trace buffer could not be copied.")) {
			info.trace_error_text = "Trace buffer copy failed.";
			failed				  = true;
			break;
		}

		output.write(reinterpret_cast<const char*>(native_buffer.data()),
					 static_cast<std::streamsize>(bytes_read));
		if (!output) {
			info.trace_error_text = "Trace output write failed.";
			add_diagnostic(diagnostics,
						   "android-previous-exit-trace-write-failed",
						   "Android previous-exit trace could not be written.",
						   info.trace_path.string());
			failed = true;
			break;
		}
		copied += static_cast<std::uint64_t>(bytes_read);
	}

	output.flush();
	if (!output && !failed) {
		info.trace_error_text = "Trace output flush failed.";
		add_diagnostic(diagnostics, "android-previous-exit-trace-flush-failed",
					   "Android previous-exit trace could not be flushed.",
					   info.trace_path.string());
		failed = true;
	}

	output.close();
	if (!output && !failed) {
		info.trace_error_text = "Trace output close failed.";
		add_diagnostic(
			diagnostics, "android-previous-exit-trace-close-file-failed",
			"Android previous-exit trace could not be closed after writing.",
			info.trace_path.string());
		failed = true;
	}

	if (failed) {
		remove_partial_trace(info.trace_path);
		return;
	}

	info.trace_captured	  = true;
	info.trace_byte_count = copied;
}

void capture_trace_if_requested(JNIEnv* env, jobject record,
								AndroidPreviousExitInfo& info,
								std::vector<core::Diagnostic>& diagnostics) {
	if (!info.trace_requested)
		return;

	juce::LocalRef<jclass> record_class =
		find_class(env, "android/app/ApplicationExitInfo", diagnostics,
				   "android-previous-exit-info-class-unavailable",
				   "Android ApplicationExitInfo class is unavailable.");
	jmethodID get_trace_stream =
		method_id(env, record_class.get(), "getTraceInputStream",
				  "()Ljava/io/InputStream;", diagnostics,
				  "android-previous-exit-trace-method-unavailable",
				  "Android previous-exit trace method is unavailable.");
	if (get_trace_stream == nullptr)
		return;

	juce::LocalRef<jobject> stream{
		env->CallObjectMethod(record, get_trace_stream)};
	if (clear_jni_exception(
			env, diagnostics, "android-previous-exit-trace-query-failed",
			"Android previous-exit trace stream could not be queried.")) {
		info.trace_error_text = "Trace stream query failed.";
		return;
	}
	if (stream == nullptr) {
		add_diagnostic(diagnostics, "android-previous-exit-trace-unavailable",
					   "Android did not return a previous-exit trace stream.");
		return;
	}

	info.trace_available				= true;
	juce::LocalRef<jclass> stream_class = find_class(
		env, "java/io/InputStream", diagnostics,
		"android-previous-exit-trace-stream-class-unavailable",
		"Java InputStream class is unavailable for previous-exit trace.");
	jmethodID close_method =
		method_id(env, stream_class.get(), "close", "()V", diagnostics,
				  "android-previous-exit-trace-close-method-unavailable",
				  "Java InputStream.close method is unavailable for "
				  "previous-exit trace.");
	copy_trace_stream(env, stream.get(), info, diagnostics);
	close_trace_stream(env, stream.get(), close_method, diagnostics);
}
#endif
}	 // namespace

PlatformValueResult<AndroidPreviousExitInfo>
JuceAndroidPreviousExitService::query_previous_exit(
	const AndroidPreviousExitQueryRequest& request) {
#if JUCE_ANDROID
	std::vector<core::Diagnostic> diagnostics;
	JNIEnv* env = juce::getEnv();
	if (env == nullptr) {
		AndroidPreviousExitInfo info = unavailable_info(request);
		add_diagnostic(
			diagnostics, "android-previous-exit-jni-unavailable",
			"Android JNI environment is unavailable for previous-exit query.");
		return platform_value_success(info, diagnostics);
	}

	juce::LocalRef<jobject> manager = activity_manager(env, diagnostics);
	if (manager == nullptr)
		return platform_value_success(unavailable_info(request), diagnostics);

	std::optional<juce::LocalRef<jobject>> record =
		newest_exit_record(env, manager.get(), diagnostics);
	if (!record.has_value())
		return platform_value_success(unavailable_info(request), diagnostics);

	AndroidPreviousExitInfo info{
		.record_available = true,
		.captured_at	  = request.captured_at,
		.reason_name	  = std::string{android_exit_reason_name(
			android_application_exit_reason_unknown)},
		.trace_requested  = request.capture_trace,
		.trace_path		  = request.trace_output_path};
	read_exit_record_fields(env, record->get(), info, diagnostics);
	capture_trace_if_requested(env, record->get(), info, diagnostics);
	return platform_value_success(info, diagnostics);
#else
	NoOpAndroidPreviousExitService fallback;
	return fallback.query_previous_exit(request);
#endif
}
}	 // namespace shuba::platform
