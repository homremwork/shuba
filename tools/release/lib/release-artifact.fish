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
        tool.apkanalyzer.path tool.apkanalyzer.version tool.apkanalyzer.sha256 \
        tool.aapt2.path tool.aapt2.sha256 tool.apksigner.path tool.apksigner.sha256 \
        tool.zipalign.path tool.zipalign.sha256 tool.llvm_readelf.path tool.llvm_readelf.sha256 \
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
            android.abi android.ndk_version android.build_tools_version android.cmake_version \
            android.gradle_version android.gradle_plugin_version android.command_line_tools_version
            printf '%s=%s\n' $shuba_key (shuba_contract_get $shuba_key)
        end
        printf 'android.gradle_wrapper_distribution=%s\n' $shuba_distribution
        printf 'tool.java.home=%s\ntool.java.version=%s\ntool.java.sha256=%s\n' \
            $shuba_java_home $shuba_java_version_output (shuba_sha256_file $shuba_java_path)
        printf 'tool.javac.version=%s\ntool.javac.sha256=%s\n' $shuba_javac_version (shuba_sha256_file $shuba_javac_path)
        printf 'tool.git.path=%s\ntool.git.version=%s\ntool.git.sha256=%s\n' \
            $shuba_git_path (shuba_first_line $shuba_git_path --version) (shuba_sha256_file $shuba_git_path)
        for shuba_tool_name in aapt2 apksigner zipalign llvm_readelf
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
