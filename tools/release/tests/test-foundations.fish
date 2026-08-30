#!/usr/bin/fish --no-config

function shuba_test_fail
    printf 'R12F foundation tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_test_require --argument-names shuba_condition shuba_message
    if test "$shuba_condition" != true
        shuba_test_fail $shuba_message
        return 1
    end
end

function shuba_test_mutated_contract --argument-names shuba_source shuba_pattern shuba_replacement shuba_expected_fragment
    set --local shuba_mutated $shuba_test_root/(random).properties
    sed "s|$shuba_pattern|$shuba_replacement|" $shuba_source >$shuba_mutated; or return 1
    set --local shuba_diagnostic (shuba_contract_load $shuba_mutated 2>&1)
    set --local shuba_status $status
    if test $shuba_status -eq 0
        shuba_test_fail "mutated contract was accepted: $shuba_pattern"
        return 1
    end
    if not string match --quiet "*$shuba_expected_fragment*" -- (string join \n -- $shuba_diagnostic)
        shuba_test_fail "mutated contract produced the wrong diagnostic: $shuba_pattern"
        return 1
    end
end

function shuba_test_write_android_command_line_tools --argument-names shuba_sdk_root shuba_directory shuba_version
    set --local shuba_tools_root $shuba_sdk_root/cmdline-tools/$shuba_directory
    mkdir -p $shuba_tools_root/bin; or return 1
    printf 'Pkg.Revision = %s\n' $shuba_version >$shuba_tools_root/source.properties; or return 1
    printf '%s\n' '#!/bin/sh' 'exit 0' >$shuba_tools_root/bin/apkanalyzer; or return 1
    chmod 0700 $shuba_tools_root/bin/apkanalyzer; or return 1
    printf '%s\n' '#!/bin/sh' 'if [ "$1" = "--version" ]; then printf "%s\\n" 1.0; exit 0; fi' 'exit 1' >$shuba_tools_root/bin/sdkmanager; or return 1
    chmod 0700 $shuba_tools_root/bin/sdkmanager; or return 1
end

function shuba_test_resolve_android_command_line_tools --argument-names shuba_sdk_root
    set --global shuba_android_sdk_root $shuba_sdk_root
    set --global shuba_release_contract_keys android.command_line_tools_versions
    set --global shuba_release_contract_values 20.0,12.0
    shuba_resolve_android_command_line_tools_root
end

function shuba_test_main
    set --local shuba_with_android false
    if test (count $argv) -eq 1; and test $argv[1] = --with-android-toolchain
        set shuba_with_android true
    else if test (count $argv) -ne 0
        shuba_test_fail 'usage: test-foundations.fish [--with-android-toolchain]'
        return 1
    end
    set --global shuba_test_root (mktemp --directory /tmp/shuba-r12f-foundations.XXXXXX); or return 1
    set --local shuba_script_directory (status dirname)
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    set --local shuba_contract $shuba_project_root/release/release.properties

    shuba_version_at_least 4.8.0 4.8.0; or return 1
    shuba_version_at_least 4.8.1 4.8.0; or return 1
    if shuba_version_at_least 4.7.9 4.8.0
        shuba_test_fail 'semantic version comparison accepted an older version'
        return 1
    end
    shuba_contract_load $shuba_contract; or return 1
    shuba_validate_structured_tools; or return 1
    if test $shuba_with_android = true
        shuba_validate_android_toolchain; or return 1
        set --local shuba_libjxl_fingerprint_output \
            ($shuba_project_root/tools/release/build-libjxl-android.fish --print-fingerprint); or return 1
        if test (count $shuba_libjxl_fingerprint_output) -ne 1; or not string match --regex --quiet \
                '^[0-9a-f]{64}$' -- $shuba_libjxl_fingerprint_output[1]
            shuba_test_fail 'Android libjxl fingerprint mode did not emit exactly one SHA-256 line'
            return 1
        end
    end

    set --local shuba_version_code (shuba_contract_get app.version_code); or return 1
    shuba_test_mutated_contract $shuba_contract '^contract.schema_version=4$' 'contract.schema_version=3' 'unsupported release contract schema'; or return 1
    shuba_test_mutated_contract $shuba_contract "^app.version_code=$shuba_version_code\$" 'app.version_code=0' 'invalid value for app.version_code'; or return 1
    shuba_test_mutated_contract $shuba_contract '^tool.jq_min_version=1.7.0$' 'tool.jq_min_version=01.7.0' 'invalid value for tool.jq_min_version'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.command_line_tools_versions=20.0,12.0$' 'android.command_line_tools_versions=20.0,20.0' 'duplicate Android command-line tools version'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.command_line_tools_versions=20.0,12.0$' 'android.command_line_tools_versions=20.0,12.0,' 'invalid Android command-line tools version'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.target_sdk=34$' 'android.target_sdk=35' 'android.target_sdk exceeds android.compile_sdk'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.abi=arm64-v8a$' 'android.abi=x86_64' 'native CPU policy supports only arm64-v8a'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.native_cpu=cortex-a73$' 'android.native_cpu=cortex-a53' 'unsupported android.native_cpu'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.native_feature_floor=armv8-a+neon+aes+sha2+crc32$' 'android.native_feature_floor=armv8-a+neon' 'android.native_feature_floor differs'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.release_optimization=O3$' 'android.release_optimization=Ofast' 'must use safe O3 optimization'; or return 1
    cat $shuba_contract $shuba_contract >$shuba_test_root/duplicate.properties
    if shuba_contract_load $shuba_test_root/duplicate.properties >/dev/null 2>&1
        shuba_test_fail 'duplicate contract keys were accepted'
        return 1
    end
    cp $shuba_contract $shuba_test_root/unknown.properties
    printf 'unknown.value=1\n' >>$shuba_test_root/unknown.properties
    if shuba_contract_load $shuba_test_root/unknown.properties >/dev/null 2>&1
        shuba_test_fail 'unknown contract key was accepted'
        return 1
    end
    shuba_contract_load $shuba_contract; or return 1

    set --local shuba_android_tools_root $shuba_test_root/android-sdk
    mkdir -p $shuba_android_tools_root/cmdline-tools; or return 1
    shuba_test_write_android_command_line_tools $shuba_android_tools_root latest 20.0; or return 1
    set --local shuba_resolved_tools (shuba_test_resolve_android_command_line_tools $shuba_android_tools_root); or return 1
    if test "$shuba_resolved_tools" != "$shuba_android_tools_root/cmdline-tools/latest"
        shuba_test_fail 'metadata-checked latest command-line tools 20.0 was not selected'
        return 1
    end
    shuba_test_write_android_command_line_tools $shuba_android_tools_root 20.0 20.0; or return 1
    if shuba_test_resolve_android_command_line_tools $shuba_android_tools_root >/dev/null 2>&1
        shuba_test_fail 'duplicate command-line tools 20.0 candidates were accepted'
        return 1
    end
    rm -rf -- $shuba_android_tools_root/cmdline-tools/20.0
    shuba_test_write_android_command_line_tools $shuba_android_tools_root 12.0 12.0; or return 1
    set shuba_resolved_tools (shuba_test_resolve_android_command_line_tools $shuba_android_tools_root); or return 1
    if test "$shuba_resolved_tools" != "$shuba_android_tools_root/cmdline-tools/latest"
        shuba_test_fail 'allowed command-line tools 20.0 did not retain precedence over 12.0'
        return 1
    end
    rm -rf -- $shuba_android_tools_root/cmdline-tools/latest
    set shuba_resolved_tools (shuba_test_resolve_android_command_line_tools $shuba_android_tools_root); or return 1
    if test "$shuba_resolved_tools" != "$shuba_android_tools_root/cmdline-tools/12.0"
        shuba_test_fail 'named command-line tools 12.0 fallback was not selected'
        return 1
    end
    rm -rf -- $shuba_android_tools_root/cmdline-tools/12.0
    shuba_test_write_android_command_line_tools $shuba_android_tools_root latest 21.0; or return 1
    if shuba_test_resolve_android_command_line_tools $shuba_android_tools_root >/dev/null 2>&1
        shuba_test_fail 'unlisted command-line tools latest revision was accepted'
        return 1
    end
    rm -f -- $shuba_android_tools_root/cmdline-tools/latest/bin/sdkmanager
    if shuba_test_resolve_android_command_line_tools $shuba_android_tools_root >/dev/null 2>&1
        shuba_test_fail 'command-line tools without sdkmanager were accepted'
        return 1
    end
    rm -rf -- $shuba_android_tools_root/cmdline-tools/latest
    shuba_test_write_android_command_line_tools $shuba_android_tools_root latest 20.0; or return 1
    printf '%s\n' 'Pkg.Revision = 20.0' >>$shuba_android_tools_root/cmdline-tools/latest/source.properties
    if shuba_test_resolve_android_command_line_tools $shuba_android_tools_root >/dev/null 2>&1
        shuba_test_fail 'command-line tools with duplicate revision metadata were accepted'
        return 1
    end

    shuba_contract_load $shuba_contract; or return 1
    set --local shuba_descriptor $shuba_test_root/tools.descriptor
    shuba_write_structured_tool_descriptor $shuba_descriptor; or return 1
    test -s $shuba_descriptor; or return 1
    grep --quiet '^tool.xmlstarlet.version=unreported$' $shuba_descriptor; or return 1
    grep --quiet '^tool.xmlstarlet.version_output_sha256=[0-9a-f]\{64\}$' $shuba_descriptor; or return 1
    if test $shuba_with_android = true
        grep --quiet '^tool.android_command_line_tools.allowed_versions=20.0,12.0$' $shuba_descriptor; or return 1
        grep --quiet '^tool.android_command_line_tools.selected_version=\(20.0\|12.0\)$' $shuba_descriptor; or return 1
        grep --quiet '^tool.apkanalyzer.sha256=[0-9a-f]\{64\}$' $shuba_descriptor; or return 1
        grep --quiet '^tool.sdkmanager.sha256=[0-9a-f]\{64\}$' $shuba_descriptor; or return 1
    else if grep --quiet '^tool.\(android_command_line_tools\|apkanalyzer\|sdkmanager\)[.]' $shuba_descriptor
        shuba_test_fail 'hermetic structured-tool descriptor unexpectedly contains Android tooling'
        return 1
    end

    set --local shuba_repository $shuba_test_root/repository
    mkdir $shuba_repository
    git -C $shuba_repository init --quiet
    git -C $shuba_repository config user.name 'Shuba Test'
    git -C $shuba_repository config user.email 'shuba-test@example.invalid'
    printf 'tracked\n' >$shuba_repository/tracked
    git -C $shuba_repository add tracked
    git -C $shuba_repository commit --quiet --message initial
    set --local shuba_clean_digest (shuba_repository_state_digest $shuba_repository); or return 1
    printf 'untracked\n' >"$shuba_repository/untracked
name"
    set --local shuba_dirty_digest (shuba_repository_state_digest $shuba_repository); or return 1
    if test "$shuba_clean_digest" = "$shuba_dirty_digest"
        shuba_test_fail 'NUL-safe repository digest ignored an untracked newline path'
        return 1
    end

    mkdir $shuba_test_root/inventory
    printf 'a\n' >$shuba_test_root/inventory/a
    shuba_write_regular_file_inventory $shuba_test_root/inventory.txt $shuba_test_root inventory; or return 1
    grep --quiet '^file|644|[0-9a-f]\{64\}|inventory/a$' $shuba_test_root/inventory.txt; or return 1
    printf 'b\n' >"$shuba_test_root/inventory/b
unsafe"
    if shuba_write_regular_file_inventory $shuba_test_root/unsafe.txt $shuba_test_root inventory >/dev/null 2>&1
        shuba_test_fail 'bounded text inventory accepted a newline path'
        return 1
    end

    printf '%s\n' 'R12F core, contract, structured-tool, Android-tool, repository-state, fingerprint, and inventory foundation tests: passed'
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-contract.fish
source $shuba_script_directory/../lib/android-toolchain.fish
source $shuba_script_directory/../lib/repository-state.fish
source $shuba_script_directory/../lib/fingerprint.fish

set --global shuba_test_root ''
function shuba_test_cleanup --on-event fish_exit
    if test -n "$shuba_test_root"; and test -d $shuba_test_root
        rm -rf -- $shuba_test_root
    end
end

shuba_test_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
