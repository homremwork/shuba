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

function shuba_test_main
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
    shuba_validate_android_toolchain; or return 1

    shuba_test_mutated_contract $shuba_contract '^contract.schema_version=2$' 'contract.schema_version=1' 'unsupported release contract schema'; or return 1
    shuba_test_mutated_contract $shuba_contract '^app.version_code=1$' 'app.version_code=0' 'invalid value for app.version_code'; or return 1
    shuba_test_mutated_contract $shuba_contract '^tool.jq_min_version=1.7.0$' 'tool.jq_min_version=01.7.0' 'invalid value for tool.jq_min_version'; or return 1
    shuba_test_mutated_contract $shuba_contract '^android.target_sdk=34$' 'android.target_sdk=35' 'android.target_sdk exceeds android.compile_sdk'; or return 1
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

    set --local shuba_descriptor $shuba_test_root/tools.descriptor
    shuba_write_structured_tool_descriptor $shuba_descriptor; or return 1
    test -s $shuba_descriptor; or return 1
    grep --quiet '^tool.xmlstarlet.version=unreported$' $shuba_descriptor; or return 1
    grep --quiet '^tool.xmlstarlet.version_output_sha256=[0-9a-f]\{64\}$' $shuba_descriptor; or return 1
    grep --quiet '^tool.apkanalyzer.sha256=[0-9a-f]\{64\}$' $shuba_descriptor; or return 1

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

shuba_test_main
set --local shuba_main_status $status
exit $shuba_main_status
