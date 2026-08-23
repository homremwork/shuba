function shuba_contract_required_keys
    printf '%s\n' \
        contract.schema_version \
        app.name \
        app.application_id \
        app.version_name \
        app.version_code \
        android.min_sdk \
        android.target_sdk \
        android.compile_sdk \
        android.abi \
        android.ndk_version \
        android.build_tools_version \
        android.cmake_version \
        android.gradle_version \
        android.gradle_plugin_version \
        android.java_runtime_version \
        android.command_line_tools_versions \
        tool.fish_min_version \
        tool.jq_min_version \
        tool.bsdtar_min_version \
        artifact.basename \
        artifact.native_library_path \
        signing.certificate_sha256 \
        android.forbidden_permissions
end

function shuba_contract_get --argument-names shuba_contract_key
    set --local shuba_contract_index (contains --index -- $shuba_contract_key $shuba_release_contract_keys)
    if test $status -ne 0; or test -z "$shuba_contract_index"
        shuba_fail "release contract key is unavailable: $shuba_contract_key"
        return 1
    end
    printf '%s\n' $shuba_release_contract_values[$shuba_contract_index]
end

function shuba_contract_require_pattern --argument-names shuba_contract_key shuba_pattern
    set --local shuba_contract_value (shuba_contract_get $shuba_contract_key); or return 1
    if not string match --regex --quiet -- $shuba_pattern $shuba_contract_value
        shuba_fail "invalid value for $shuba_contract_key: $shuba_contract_value"
        return 1
    end
end

function shuba_contract_validate
    set --local shuba_required_keys (shuba_contract_required_keys); or return 1
    if test (count $shuba_release_contract_keys) -ne (count $shuba_required_keys)
        shuba_fail 'release contract key count differs from schema version 3'
        return 1
    end
    for shuba_key in $shuba_required_keys
        if not contains -- $shuba_key $shuba_release_contract_keys
            shuba_fail "release contract is missing required key: $shuba_key"
            return 1
        end
    end
    for shuba_key in $shuba_release_contract_keys
        if not contains -- $shuba_key $shuba_required_keys
            shuba_fail "release contract contains unknown key: $shuba_key"
            return 1
        end
    end

    if test (shuba_contract_get contract.schema_version) != 3
        shuba_fail 'unsupported release contract schema version'
        return 1
    end
    shuba_contract_require_pattern app.name '^[A-Za-z0-9][A-Za-z0-9._ -]*$'; or return 1
    shuba_contract_require_pattern app.application_id '^[a-z][a-z0-9_]*([.][a-z][a-z0-9_]*)+$'; or return 1
    shuba_contract_require_pattern app.version_name '^(0|[1-9][0-9]*)[.](0|[1-9][0-9]*)[.](0|[1-9][0-9]*)$'; or return 1
    shuba_contract_require_pattern app.version_code '^[1-9][0-9]*$'; or return 1

    for shuba_key in android.min_sdk android.target_sdk android.compile_sdk android.java_runtime_version
        shuba_contract_require_pattern $shuba_key '^[1-9][0-9]*$'; or return 1
    end
    set --local shuba_min_sdk (shuba_contract_get android.min_sdk); or return 1
    set --local shuba_target_sdk (shuba_contract_get android.target_sdk); or return 1
    set --local shuba_compile_sdk (shuba_contract_get android.compile_sdk); or return 1
    if test $shuba_min_sdk -gt $shuba_target_sdk
        shuba_fail 'android.min_sdk exceeds android.target_sdk'
        return 1
    end
    if test $shuba_target_sdk -gt $shuba_compile_sdk
        shuba_fail 'android.target_sdk exceeds android.compile_sdk'
        return 1
    end

    shuba_contract_require_pattern android.abi '^[A-Za-z0-9_-]+$'; or return 1
    for shuba_key in \
        android.ndk_version \
        android.build_tools_version \
        android.cmake_version \
        android.gradle_version \
        android.gradle_plugin_version \
        tool.fish_min_version \
        tool.jq_min_version \
        tool.bsdtar_min_version
        shuba_contract_require_pattern $shuba_key '^(0|[1-9][0-9]*)([.](0|[1-9][0-9]*)){1,2}$'; or return 1
    end
    set --local shuba_command_line_tools_versions \
        (string split , -- (shuba_contract_get android.command_line_tools_versions)); or return 1
    if test (count $shuba_command_line_tools_versions) -eq 0
        shuba_fail 'android.command_line_tools_versions must not be empty'
        return 1
    end
    set --local shuba_seen_command_line_tools_versions
    for shuba_version in $shuba_command_line_tools_versions
        if not string match --regex --quiet '^(0|[1-9][0-9]*)([.](0|[1-9][0-9]*)){1,2}$' -- $shuba_version
            shuba_fail "invalid Android command-line tools version: $shuba_version"
            return 1
        end
        if contains -- $shuba_version $shuba_seen_command_line_tools_versions
            shuba_fail "duplicate Android command-line tools version: $shuba_version"
            return 1
        end
        set --append shuba_seen_command_line_tools_versions $shuba_version
    end

    set --local shuba_app_name (string replace --all ' ' - -- (shuba_contract_get app.name))
    set --local shuba_expected_basename "$shuba_app_name-"(shuba_contract_get app.version_name)"-"(shuba_contract_get android.abi)'.apk'
    if test (shuba_contract_get artifact.basename) != $shuba_expected_basename
        shuba_fail "artifact.basename must be derived from app name, version, and ABI: $shuba_expected_basename"
        return 1
    end

    set --local shuba_native_path (shuba_contract_get artifact.native_library_path); or return 1
    set --local shuba_expected_native_pattern '^lib/'(shuba_contract_get android.abi)'/lib[A-Za-z0-9_]+[.]so$'
    if not string match --regex --quiet -- $shuba_expected_native_pattern $shuba_native_path
        shuba_fail "artifact.native_library_path is unsafe or uses the wrong ABI: $shuba_native_path"
        return 1
    end
    shuba_contract_require_pattern signing.certificate_sha256 '^[0-9A-F]{64}$'; or return 1

    set --local shuba_permissions (string split , -- (shuba_contract_get android.forbidden_permissions)); or return 1
    if test (count $shuba_permissions) -eq 0
        shuba_fail 'android.forbidden_permissions must not be empty'
        return 1
    end
    set --local shuba_seen_permissions
    for shuba_permission in $shuba_permissions
        if not string match --regex --quiet '^([A-Za-z][A-Za-z0-9_]*[.])+[A-Za-z][A-Za-z0-9_]*$' -- $shuba_permission
            shuba_fail "invalid forbidden permission: $shuba_permission"
            return 1
        end
        if contains -- $shuba_permission $shuba_seen_permissions
            shuba_fail "duplicate forbidden permission: $shuba_permission"
            return 1
        end
        set --append shuba_seen_permissions $shuba_permission
    end
end

function shuba_contract_load --argument-names shuba_contract_path
    shuba_require_regular_file $shuba_contract_path; or return 1
    set --local shuba_keys
    set --local shuba_values
    set --local shuba_line_number 0
    while read --delimiter \n shuba_line
        set shuba_line_number (math $shuba_line_number + 1)
        if test -z "$shuba_line"; or string match --quiet '#*' -- $shuba_line
            continue
        end
        if string match --regex --quiet '[[:cntrl:]]' -- $shuba_line
            shuba_fail "release contract line $shuba_line_number contains a forbidden control character"
            return 1
        end
        set --local shuba_entry (string split --max 1 = -- $shuba_line)
        if test (count $shuba_entry) -ne 2; or not string match --regex --quiet '^[a-z][a-z0-9_]*([.][a-z][a-z0-9_]*)*$' -- $shuba_entry[1]
            shuba_fail "release contract line $shuba_line_number is not a canonical key=value entry"
            return 1
        end
        if test -z "$shuba_entry[2]"; or string match --quiet ' *' -- $shuba_entry[2]; or string match --quiet '* ' -- $shuba_entry[2]
            shuba_fail "release contract line $shuba_line_number has an empty or whitespace-padded value"
            return 1
        end
        if contains -- $shuba_entry[1] $shuba_keys
            shuba_fail "release contract repeats key on line $shuba_line_number: $shuba_entry[1]"
            return 1
        end
        set --append shuba_keys $shuba_entry[1]
        set --append shuba_values $shuba_entry[2]
    end <$shuba_contract_path

    set --global shuba_release_contract_keys $shuba_keys
    set --global shuba_release_contract_values $shuba_values
    if not shuba_contract_validate
        set --erase --global shuba_release_contract_keys
        set --erase --global shuba_release_contract_values
        return 1
    end
end
