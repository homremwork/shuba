function shuba_artifact_expected_names --argument-names shuba_basename
    printf '%s\n' $shuba_basename $shuba_basename.sha256 \
        $shuba_basename.provenance.txt $shuba_basename.verification.txt | sort
end

function shuba_validate_artifact_directory --argument-names shuba_directory shuba_basename shuba_verifier
    if not test -d $shuba_directory; or test -L $shuba_directory
        shuba_fail "artifact output is unavailable or symbolic-linked: $shuba_directory"
        return 1
    end
    set --local shuba_expected (mktemp /tmp/shuba-artifact-expected.XXXXXX); or return 1
    set --local shuba_actual (mktemp /tmp/shuba-artifact-actual.XXXXXX); or begin
        rm -f -- $shuba_expected
        return 1
    end
    shuba_artifact_expected_names $shuba_basename >$shuba_expected
    find -P $shuba_directory -mindepth 1 -maxdepth 1 -printf '%f\n' | sort >$shuba_actual
    set --local shuba_inventory_statuses $pipestatus
    for shuba_status in $shuba_inventory_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_expected $shuba_actual
            return 1
        end
    end
    cmp --silent $shuba_expected $shuba_actual
    set --local shuba_inventory_status $status
    rm -f -- $shuba_expected $shuba_actual
    if test $shuba_inventory_status -ne 0
        shuba_fail 'artifact output has an unexpected four-file inventory'
        return 1
    end
    for shuba_name in $shuba_basename $shuba_basename.sha256 \
        $shuba_basename.provenance.txt $shuba_basename.verification.txt
        shuba_require_regular_file $shuba_directory/$shuba_name; or return 1
    end
    shuba_verify_checksum_sidecar $shuba_directory/$shuba_basename.sha256 \
        $shuba_directory/$shuba_basename $shuba_basename; or return 1
    $shuba_verifier $shuba_directory/$shuba_basename >/dev/null; or return 1
end

function shuba_historical_provenance_get --argument-names shuba_provenance_path shuba_key
    awk -v expected_key=$shuba_key '
        $0 == "release_state_begin" { exit }
        index($0, expected_key "=") == 1 {
            count += 1
            value = substr($0, length(expected_key) + 2)
        }
        END {
            if (count == 1 && length(value) > 0) {
                print value
                exit 0
            }
            exit 1
        }
    ' $shuba_provenance_path
end

function shuba_historical_provenance_require_marker --argument-names shuba_provenance_path shuba_marker
    set --local shuba_count (grep --fixed-strings --line-regexp --count -- $shuba_marker $shuba_provenance_path)
    if test $status -ne 0; or test $shuba_count != 1
        shuba_fail "historical release provenance requires exactly one marker: $shuba_marker"
        return 1
    end
end

function shuba_validate_historical_provenance_header --argument-names shuba_provenance_path
    set --local shuba_allowed_keys \
        provenance_schema_version created_utc artifact.basename artifact.sha256 artifact.bytes \
        artifact.generated_apk_sha256 artifact.generated_output_metadata_sha256 release_contract.sha256 \
        release_entrypoint.sha256 signing.certificate_sha256 app.application_id app.version_name \
        app.version_code android.min_sdk android.target_sdk android.abi android.ndk_version \
        android.native_cpu android.native_feature_floor android.release_optimization \
        android.build_tools_version android.cmake_version android.gradle_version \
        android.gradle_plugin_version android.command_line_tools_version \
        android.command_line_tools_versions android.gradle_wrapper_distribution tool.java.home \
        tool.java.version tool.java.sha256 tool.javac.version tool.javac.sha256 tool.git.path \
        tool.git.version tool.git.sha256 tool.fish.path tool.fish.version tool.fish.sha256 \
        tool.jq.path tool.jq.version tool.jq.sha256 tool.xmlstarlet.path tool.xmlstarlet.version \
        tool.xmlstarlet.resolved_path tool.xmlstarlet.sha256 \
        tool.xmlstarlet.version_output_sha256 tool.xmlstarlet.linked_library_versions_sha256 \
        tool.xmlstarlet.xpath_capability tool.bsdtar.path tool.bsdtar.version tool.bsdtar.sha256 \
        tool.android_command_line_tools.allowed_versions tool.android_command_line_tools.selected_root \
        tool.android_command_line_tools.selected_version tool.apkanalyzer.path tool.apkanalyzer.version \
        tool.apkanalyzer.sha256 tool.sdkmanager.path tool.sdkmanager.version tool.sdkmanager.sha256 \
        tool.aapt2.path tool.aapt2.sha256 tool.apksigner.path tool.apksigner.sha256 \
        tool.zipalign.path tool.zipalign.sha256 tool.llvm_readelf.path tool.llvm_readelf.sha256 \
        tool.llvm_objdump.path tool.llvm_objdump.sha256 \
        libjxl.fingerprint_sha256 projucer.fingerprint_sha256 verification.pre_publish_sha256
    set --local shuba_seen_keys
    set --local shuba_found_release_state false
    while read --delimiter \n shuba_line
        if test "$shuba_line" = release_state_begin
            set shuba_found_release_state true
            break
        end
        if string match --regex --quiet '[[:cntrl:]]' -- $shuba_line
            shuba_fail 'historical release provenance header contains a forbidden control character'
            return 1
        end
        set --local shuba_entry (string split --max 1 = -- $shuba_line)
        if test (count $shuba_entry) -ne 2; or not string match --regex --quiet '^[a-z][a-z0-9_]*([.][a-z][a-z0-9_]*)*$' -- $shuba_entry[1]
            shuba_fail 'historical release provenance header is not a canonical key=value record'
            return 1
        end
        if test -z "$shuba_entry[2]"; or string match --quiet ' *' -- $shuba_entry[2]; or string match --quiet '* ' -- $shuba_entry[2]
            shuba_fail 'historical release provenance header contains an empty or whitespace-padded value'
            return 1
        end
        if not contains -- $shuba_entry[1] $shuba_allowed_keys
            shuba_fail "historical release provenance header contains an unknown record: $shuba_entry[1]"
            return 1
        end
        if contains -- $shuba_entry[1] $shuba_seen_keys
            shuba_fail "historical release provenance header repeats record: $shuba_entry[1]"
            return 1
        end
        set --append shuba_seen_keys $shuba_entry[1]
    end <$shuba_provenance_path
    if test $shuba_found_release_state != true
        shuba_fail 'historical release provenance lacks release-state evidence'
        return 1
    end
    for shuba_key in \
        provenance_schema_version created_utc artifact.basename artifact.sha256 artifact.bytes \
        artifact.generated_apk_sha256 artifact.generated_output_metadata_sha256 release_contract.sha256 \
        release_entrypoint.sha256 signing.certificate_sha256 app.application_id app.version_name \
        app.version_code android.min_sdk android.target_sdk android.abi \
        libjxl.fingerprint_sha256 projucer.fingerprint_sha256 verification.pre_publish_sha256
        if not contains -- $shuba_key $shuba_seen_keys
            shuba_fail "historical release provenance is missing required record: $shuba_key"
            return 1
        end
    end
    set --local shuba_native_policy_keys \
        android.native_cpu android.native_feature_floor android.release_optimization
    set --local shuba_native_policy_records 0
    for shuba_key in $shuba_native_policy_keys
        contains -- $shuba_key $shuba_seen_keys; and set shuba_native_policy_records (math $shuba_native_policy_records + 1)
    end
    if test $shuba_native_policy_records -ne 0; and test $shuba_native_policy_records -ne (count $shuba_native_policy_keys)
        shuba_fail 'historical release provenance must retain either all or none of the Android native-policy records'
        return 1
    end
    if test $shuba_native_policy_records -gt 0
        if test (shuba_historical_provenance_get $shuba_provenance_path android.native_cpu) != cortex-a73
            shuba_fail 'historical release provenance has an unsupported Android native CPU policy'
            return 1
        end
        if test (shuba_historical_provenance_get $shuba_provenance_path android.native_feature_floor) != armv8-a+neon+aes+sha2+crc32
            shuba_fail 'historical release provenance has an unsupported Android native feature floor'
            return 1
        end
        if test (shuba_historical_provenance_get $shuba_provenance_path android.release_optimization) != O3
            shuba_fail 'historical release provenance has an unsupported Android Release optimization policy'
            return 1
        end
        for shuba_key in tool.llvm_objdump.path tool.llvm_objdump.sha256
            if not contains -- $shuba_key $shuba_seen_keys
                shuba_fail "historical release provenance is missing native-disassembly tool evidence: $shuba_key"
                return 1
            end
        end
    end
    set --local shuba_legacy_command_line_tools_count 0
    contains -- android.command_line_tools_version $shuba_seen_keys; and set shuba_legacy_command_line_tools_count 1
    set --local shuba_current_command_line_tools_count 0
    contains -- android.command_line_tools_versions $shuba_seen_keys; and set shuba_current_command_line_tools_count 1
    if test (math $shuba_legacy_command_line_tools_count + $shuba_current_command_line_tools_count) -ne 1
        shuba_fail 'historical release provenance must record exactly one command-line-tools policy'
        return 1
    end
end

function shuba_validate_historical_release_packet --argument-names shuba_directory
    if not test -d $shuba_directory; or test -L $shuba_directory
        shuba_fail "historical artifact output is unavailable or symbolic-linked: $shuba_directory"
        return 1
    end
    set --local shuba_links (find -P $shuba_directory -mindepth 1 -maxdepth 1 -type l -print -quit)
    if test $status -ne 0; or test -n "$shuba_links"
        shuba_fail 'historical artifact output contains a symbolic link or could not be inspected'
        return 1
    end
    set --local shuba_provenance_paths \
        (find -P $shuba_directory -mindepth 1 -maxdepth 1 -type f -name '*.apk.provenance.txt' -printf '%f\n' | sort)
    if test $status -ne 0; or test (count $shuba_provenance_paths) -ne 1
        shuba_fail 'historical artifact output requires exactly one APK provenance sidecar'
        return 1
    end
    set --local shuba_provenance_name $shuba_provenance_paths[1]
    set --local shuba_basename (string replace --regex '[.]provenance[.]txt$' '' -- $shuba_provenance_name)
    if test "$shuba_basename" = "$shuba_provenance_name"; or not string match --regex --quiet '^[A-Za-z0-9][A-Za-z0-9._+-]{0,254}[.]apk$' -- $shuba_basename
        shuba_fail 'historical artifact provenance filename does not derive a safe APK basename'
        return 1
    end
    if string match --quiet '*upgrade*' -- (string lower -- $shuba_basename)
        shuba_fail 'historical final artifact must not be an upgrade probe'
        return 1
    end
    set --local shuba_provenance_path $shuba_directory/$shuba_provenance_name
    shuba_require_regular_file $shuba_provenance_path; or return 1
    shuba_validate_historical_provenance_header $shuba_provenance_path; or return 1

    set --local shuba_provenance_basename (shuba_historical_provenance_get $shuba_provenance_path artifact.basename); or begin
        shuba_fail 'historical release provenance does not provide one artifact basename'
        return 1
    end
    if test "$shuba_provenance_basename" != "$shuba_basename"
        shuba_fail 'historical provenance basename differs from its packet filename'
        return 1
    end
    set --local shuba_expected (mktemp /tmp/shuba-historical-artifact-expected.XXXXXX); or return 1
    set --local shuba_actual (mktemp /tmp/shuba-historical-artifact-actual.XXXXXX); or begin
        rm -f -- $shuba_expected
        return 1
    end
    shuba_artifact_expected_names $shuba_basename >$shuba_expected
    find -P $shuba_directory -mindepth 1 -maxdepth 1 -printf '%f\n' | sort >$shuba_actual
    set --local shuba_inventory_statuses $pipestatus
    for shuba_status in $shuba_inventory_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_expected $shuba_actual
            return 1
        end
    end
    cmp --silent $shuba_expected $shuba_actual
    set --local shuba_inventory_status $status
    rm -f -- $shuba_expected $shuba_actual
    if test $shuba_inventory_status -ne 0
        shuba_fail 'historical artifact output has an unexpected four-file inventory'
        return 1
    end
    for shuba_name in $shuba_basename $shuba_basename.sha256 $shuba_basename.provenance.txt $shuba_basename.verification.txt
        set --local shuba_path $shuba_directory/$shuba_name
        shuba_require_regular_file $shuba_path; or return 1
        if test (stat --format %a -- $shuba_path) != 644
            shuba_fail "historical artifact packet file mode is not public-safe: $shuba_name"
            return 1
        end
    end
    set --local shuba_apk_path $shuba_directory/$shuba_basename
    shuba_verify_checksum_sidecar $shuba_directory/$shuba_basename.sha256 $shuba_apk_path $shuba_basename; or return 1

    set --local shuba_artifact_digest (shuba_historical_provenance_get $shuba_provenance_path artifact.sha256); or return 1
    set --local shuba_generated_digest (shuba_historical_provenance_get $shuba_provenance_path artifact.generated_apk_sha256); or return 1
    for shuba_digest in $shuba_artifact_digest $shuba_generated_digest
        if not string match --regex --quiet '^[0-9a-f]{64}$' -- $shuba_digest
            shuba_fail 'historical release provenance contains an invalid APK SHA-256'
            return 1
        end
    end
    set --local shuba_actual_digest (shuba_sha256_file $shuba_apk_path); or return 1
    if test "$shuba_artifact_digest" != "$shuba_actual_digest"; or test "$shuba_generated_digest" != "$shuba_actual_digest"
        shuba_fail 'historical APK bytes differ from retained provenance'
        return 1
    end
    set --local shuba_artifact_bytes (shuba_historical_provenance_get $shuba_provenance_path artifact.bytes); or return 1
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_artifact_bytes; or test $shuba_artifact_bytes != (stat --format %s -- $shuba_apk_path)
        shuba_fail 'historical APK size differs from retained provenance'
        return 1
    end

    set --local shuba_application_id (shuba_historical_provenance_get $shuba_provenance_path app.application_id); or return 1
    set --local shuba_version_name (shuba_historical_provenance_get $shuba_provenance_path app.version_name); or return 1
    set --local shuba_version_code (shuba_historical_provenance_get $shuba_provenance_path app.version_code); or return 1
    set --local shuba_min_sdk (shuba_historical_provenance_get $shuba_provenance_path android.min_sdk); or return 1
    set --local shuba_target_sdk (shuba_historical_provenance_get $shuba_provenance_path android.target_sdk); or return 1
    set --local shuba_abi (shuba_historical_provenance_get $shuba_provenance_path android.abi); or return 1
    set --local shuba_certificate (shuba_historical_provenance_get $shuba_provenance_path signing.certificate_sha256); or return 1
    if test "$shuba_application_id" != (shuba_contract_get app.application_id)
        shuba_fail 'historical APK application ID differs from the successor contract'
        return 1
    end
    if test "$shuba_abi" != (shuba_contract_get android.abi)
        shuba_fail 'historical APK ABI differs from the successor contract'
        return 1
    end
    if test "$shuba_certificate" != (shuba_contract_get signing.certificate_sha256)
        shuba_fail 'historical APK signing certificate differs from the successor contract'
        return 1
    end
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_version_code; or test $shuba_version_code -ge (shuba_contract_get app.version_code)
        shuba_fail 'historical APK version code is not lower than the successor contract'
        return 1
    end

    set --local shuba_verification_path $shuba_directory/$shuba_basename.verification.txt
    set --local shuba_verification_lines (string split \n <$shuba_verification_path)
    if test (count $shuba_verification_lines) -ne 7; or test "$shuba_verification_lines[1]" != verification_evidence_schema_version=1; or test "$shuba_verification_lines[2]" != pre_publication_verification_begin; or test "$shuba_verification_lines[4]" != pre_publication_verification_end; or test "$shuba_verification_lines[5]" != post_publication_verification_begin; or test "$shuba_verification_lines[7]" != post_publication_verification_end; or test "$shuba_verification_lines[3]" != "$shuba_verification_lines[6]"
        shuba_fail 'historical release verification evidence does not bind its retained identity'
        return 1
    end
    set --local shuba_verification_line $shuba_verification_lines[3]
    set --local shuba_verification_fields (string match --regex --groups-only \
        '^Android APK verification: (.+) ((0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)) code ([1-9][0-9]*) [(]([A-Za-z0-9_-]+)[)] is signed, aligned, contract-consistent, and stripped[.]$' \
        -- $shuba_verification_line)
    if test (count $shuba_verification_fields) -ne 7
        set shuba_verification_fields (string match --regex --groups-only \
            '^Android APK verification: (.+) ((0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)) code ([1-9][0-9]*) [(]([A-Za-z0-9_-]+)[)] is signed, aligned, contract-consistent, stripped, and AArch64-disassembled[.]$' \
            -- $shuba_verification_line)
    end
    if test (count $shuba_verification_fields) -ne 7; or test "$shuba_verification_fields[2]" != "$shuba_version_name"; or test "$shuba_verification_fields[6]" != "$shuba_version_code"; or test "$shuba_verification_fields[7]" != "$shuba_abi"
        shuba_fail 'historical release verification evidence does not bind its retained identity'
        return 1
    end
    set --local shuba_application_label $shuba_verification_fields[1]
    set --local shuba_preverification_digest (shuba_historical_provenance_get $shuba_provenance_path verification.pre_publish_sha256); or return 1
    set --local shuba_expected_preverification_digest \
        (printf '%s\n' "$shuba_verification_line" | sha256sum | string split --max 1 ' ')[1]
    if test "$shuba_preverification_digest" != "$shuba_expected_preverification_digest"
        shuba_fail 'historical provenance pre-publication verification digest is inconsistent'
        return 1
    end
    for shuba_marker in release_state_begin release_state_end submodule_state_begin submodule_state_end \
        build_input_manifest_begin build_input_manifest_end
        shuba_historical_provenance_require_marker $shuba_provenance_path $shuba_marker; or return 1
    end
    for shuba_record in release_state_schema_version=1 submodule_state_schema_version=1 build_input_manifest_schema_version=1
        grep --fixed-strings --line-regexp --quiet -- $shuba_record $shuba_provenance_path; or begin
            shuba_fail "historical release provenance lacks required immutable-state record: $shuba_record"
            return 1
        end
    end
    if not set --query shuba_release_project_root
        shuba_fail 'historical artifact validation requires the release project root'
        return 1
    end
    shuba_validate_historical_android_apk $shuba_release_project_root $shuba_apk_path \
        $shuba_basename $shuba_application_id $shuba_version_code $shuba_version_name \
        $shuba_min_sdk $shuba_target_sdk $shuba_abi $shuba_certificate $shuba_application_label; or return 1
end

function shuba_write_checksum_sidecar --argument-names shuba_path shuba_digest shuba_basename
    if not string match --regex --quiet '^[0-9a-f]{64}$' -- $shuba_digest
        shuba_fail 'artifact checksum sidecar requires a valid SHA-256 digest'
        return 1
    end
    if string match --regex --quiet '[[:cntrl:]/]' -- $shuba_basename
        shuba_fail 'artifact checksum sidecar basename is unsafe'
        return 1
    end
    printf '%s  %s\n' $shuba_digest $shuba_basename >$shuba_path
    and chmod 0644 $shuba_path
end

function shuba_verify_checksum_sidecar --argument-names shuba_checksum_path shuba_apk_path shuba_basename
    shuba_require_regular_file $shuba_checksum_path; or return 1
    shuba_require_regular_file $shuba_apk_path; or return 1
    set --local shuba_expected (printf '%s  %s\n' (shuba_sha256_file $shuba_apk_path) $shuba_basename | string collect)
    set --local shuba_actual (string collect <$shuba_checksum_path)
    if test "$shuba_actual" != "$shuba_expected"
        shuba_fail 'artifact checksum sidecar does not match the APK bytes'
        return 1
    end
end

function shuba_read_builder_fingerprint --argument-names shuba_stamp_path shuba_label
    set --local shuba_fingerprint (shuba_read_stamp_fingerprint $shuba_stamp_path 2); or begin
        shuba_fail "$shuba_label stamp is invalid"
        return 1
    end
    printf '%s\n' $shuba_fingerprint
end

function shuba_validate_release_provenance --argument-names shuba_provenance_path
    shuba_require_regular_file $shuba_provenance_path; or return 1
    set --local shuba_empty_records (grep --extended-regexp \
        '^[A-Za-z][A-Za-z0-9_.-]*=$' $shuba_provenance_path)
    set --local shuba_empty_status $status
    if test $shuba_empty_status -gt 1
        shuba_fail 'could not inspect release provenance for empty records'
        return 1
    end
    if test $shuba_empty_status -eq 0
        shuba_fail 'release provenance contains an empty machine-readable value'
        return 1
    end
    set --local shuba_malformed_tool_records (grep --fixed-strings 'tool..' $shuba_provenance_path)
    set --local shuba_malformed_tool_status $status
    if test $shuba_malformed_tool_status -gt 1
        shuba_fail 'could not inspect release provenance tool records'
        return 1
    end
    if test $shuba_malformed_tool_status -eq 0
        shuba_fail 'release provenance contains a malformed tool record'
        return 1
    end
    for shuba_key in \
        provenance_schema_version artifact.basename artifact.sha256 artifact.bytes \
        release_contract.sha256 release_entrypoint.sha256 signing.certificate_sha256 \
        tool.java.home tool.java.version tool.java.sha256 tool.javac.version tool.javac.sha256 \
        tool.git.path tool.git.version tool.git.sha256 \
        tool.fish.path tool.fish.version tool.fish.sha256 \
        tool.jq.path tool.jq.version tool.jq.sha256 \
        tool.xmlstarlet.path tool.xmlstarlet.resolved_path tool.xmlstarlet.sha256 \
        tool.xmlstarlet.version_output_sha256 tool.xmlstarlet.linked_library_versions_sha256 \
        tool.xmlstarlet.xpath_capability tool.bsdtar.path tool.bsdtar.version tool.bsdtar.sha256 \
        tool.android_command_line_tools.allowed_versions tool.android_command_line_tools.selected_root \
        tool.android_command_line_tools.selected_version \
        tool.apkanalyzer.path tool.apkanalyzer.version tool.apkanalyzer.sha256 \
        tool.sdkmanager.path tool.sdkmanager.version tool.sdkmanager.sha256 \
        tool.aapt2.path tool.aapt2.sha256 tool.apksigner.path tool.apksigner.sha256 \
        tool.zipalign.path tool.zipalign.sha256 tool.llvm_readelf.path tool.llvm_readelf.sha256 \
        tool.llvm_objdump.path tool.llvm_objdump.sha256 \
        android.native_cpu android.native_feature_floor android.release_optimization \
        libjxl.fingerprint_sha256 projucer.fingerprint_sha256 verification.pre_publish_sha256
        set --local shuba_records (awk -F= -v key=$shuba_key \
            '$1 == key && length($0) > length(key) + 1 { print }' $shuba_provenance_path)
        set --local shuba_record_status $status
        if test $shuba_record_status -gt 1
            shuba_fail "could not inspect required release provenance record: $shuba_key"
            return 1
        end
        if test $shuba_record_status -ne 0; or test (count $shuba_records) -ne 1
            shuba_fail "release provenance requires exactly one non-empty record: $shuba_key"
            return 1
        end
    end
    for shuba_marker in release_state_begin release_state_end submodule_state_begin \
        submodule_state_end build_input_manifest_begin build_input_manifest_end
        set --local shuba_records (grep --fixed-strings --line-regexp $shuba_marker $shuba_provenance_path)
        set --local shuba_record_status $status
        if test $shuba_record_status -gt 1
            shuba_fail "could not inspect release provenance marker: $shuba_marker"
            return 1
        end
        if test $shuba_record_status -ne 0; or test (count $shuba_records) -ne 1
            shuba_fail "release provenance requires exactly one marker: $shuba_marker"
            return 1
        end
    end
    set --local shuba_secret_records (grep --extended-regexp \
        'SHUBA_ANDROID_(STORE|KEY)_PASSWORD|ORG_GRADLE_PROJECT_shubaRelease(Store|Key)Password' \
        $shuba_provenance_path)
    set --local shuba_secret_status $status
    if test $shuba_secret_status -gt 1
        shuba_fail 'could not inspect release provenance secret boundary'
        return 1
    end
    if test $shuba_secret_status -eq 0
        shuba_fail 'release provenance contains a signing-password variable name'
        return 1
    end
end

function shuba_write_release_provenance --argument-names shuba_output shuba_project_root shuba_generated_apk shuba_apk_digest shuba_apk_size shuba_state_directory shuba_preverification
    set --local shuba_wrapper_properties $shuba_project_root/Builds/Android/gradle/wrapper/gradle-wrapper.properties
    shuba_require_regular_file $shuba_wrapper_properties; or return 1
    set --local shuba_distribution (sed -n 's/^distributionUrl=//p' $shuba_wrapper_properties)
    if test (count $shuba_distribution) -ne 1; or string match --regex --quiet '[[:cntrl:]]' -- $shuba_distribution
        shuba_fail 'generated Gradle wrapper distribution URL is malformed'
        return 1
    end
    set --local shuba_tools $shuba_output.tools
    shuba_write_structured_tool_descriptor $shuba_tools; or return 1
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    begin
        printf 'provenance_schema_version=2\n'
        printf 'created_utc=%s\n' (date --utc +%Y-%m-%dT%H:%M:%SZ)
        printf 'artifact.basename=%s\n' (shuba_contract_get artifact.basename)
        printf 'artifact.sha256=%s\nartifact.bytes=%s\n' $shuba_apk_digest $shuba_apk_size
        printf 'artifact.generated_apk_sha256=%s\n' $shuba_apk_digest
        printf 'artifact.generated_output_metadata_sha256=%s\n' \
            (shuba_sha256_file (path dirname $shuba_generated_apk)/output-metadata.json)
        printf 'release_contract.sha256=%s\n' (shuba_sha256_file $shuba_project_root/release/release.properties)
        printf 'release_entrypoint.sha256=%s\n' (shuba_sha256_file $shuba_project_root/tools/release/build-android-release.fish)
        printf 'signing.certificate_sha256=%s\n' (shuba_contract_get signing.certificate_sha256)
        for shuba_key in app.application_id app.version_name app.version_code android.min_sdk android.target_sdk \
            android.abi android.native_cpu android.native_feature_floor android.release_optimization \
            android.ndk_version android.build_tools_version android.cmake_version \
            android.gradle_version android.gradle_plugin_version android.command_line_tools_versions
            printf '%s=%s\n' $shuba_key (shuba_contract_get $shuba_key)
        end
        printf 'android.gradle_wrapper_distribution=%s\n' $shuba_distribution
        printf 'tool.java.home=%s\ntool.java.version=%s\ntool.java.sha256=%s\n' \
            $shuba_java_home $shuba_java_version_output (shuba_sha256_file $shuba_java_path)
        printf 'tool.javac.version=%s\ntool.javac.sha256=%s\n' $shuba_javac_version (shuba_sha256_file $shuba_javac_path)
        printf 'tool.git.path=%s\ntool.git.version=%s\ntool.git.sha256=%s\n' \
            $shuba_git_path (shuba_first_line $shuba_git_path --version) (shuba_sha256_file $shuba_git_path)
        for shuba_tool_name in aapt2 apksigner zipalign llvm_readelf llvm_objdump
            set --local shuba_tool_path
            switch $shuba_tool_name
                case aapt2
                    set shuba_tool_path $shuba_aapt2_path
                case apksigner
                    set shuba_tool_path $shuba_apksigner_path
                case zipalign
                    set shuba_tool_path $shuba_zipalign_path
                case llvm_readelf
                    set shuba_tool_path $shuba_ndk_readelf_path
                case llvm_objdump
                    set shuba_tool_path $shuba_ndk_objdump_path
            end
            set --local shuba_tool_resolved_path \
                (realpath --canonicalize-existing -- $shuba_tool_path); or return 1
            shuba_require_executable_file $shuba_tool_resolved_path; or return 1
            printf 'tool.%s.path=%s\ntool.%s.sha256=%s\n' $shuba_tool_name $shuba_tool_resolved_path \
                $shuba_tool_name (shuba_sha256_file $shuba_tool_resolved_path)
        end
        printf 'libjxl.fingerprint_sha256=%s\n' $shuba_libjxl_fingerprint
        printf 'projucer.fingerprint_sha256=%s\n' $shuba_projucer_fingerprint
        printf 'verification.pre_publish_sha256=%s\n' (shuba_sha256_file $shuba_preverification)
        cat -- $shuba_tools
        printf '%s\n' release_state_begin
        cat -- $shuba_state_directory/release.state
        printf '%s\n' release_state_end submodule_state_begin
        cat -- $shuba_state_directory/submodules.state
        printf '%s\n' submodule_state_end build_input_manifest_begin
        cat -- $shuba_state_directory/build-inputs.manifest
        printf '%s\n' build_input_manifest_end
    end >$shuba_output
    set --local shuba_provenance_status $status
    rm -f -- $shuba_tools
    if test $shuba_provenance_status -eq 0
        chmod 0644 $shuba_output
        and shuba_validate_release_provenance $shuba_output
        set shuba_provenance_status $status
    end
    return $shuba_provenance_status
end
