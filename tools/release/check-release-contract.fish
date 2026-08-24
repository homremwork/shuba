#!/usr/bin/fish --no-config

function shuba_check_release_contract_main
    if test (count $argv) -ne 0
        shuba_fail 'release contract check accepts no arguments'
        return 1
    end
    set --local shuba_script_directory (status dirname)
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../..); or return 1
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_require_fish_version (shuba_contract_get tool.fish_min_version); or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_validate_android_sdk_layout; or return 1
    shuba_validate_android_native_policy; or return 1
    printf 'release contract check: schema %s describes %s %s (%s), code %s, API %s, ABI %s, CPU %s, Release -%s; fish %s, jq %s, XMLStarlet family/capability, bsdtar %s, and Android command-line tools %s selected from %s passed.\n' \
        (shuba_contract_get contract.schema_version) \
        (shuba_contract_get app.name) \
        (shuba_contract_get app.version_name) \
        (shuba_contract_get app.application_id) \
        (shuba_contract_get app.version_code) \
        (shuba_contract_get android.target_sdk) \
        (shuba_contract_get android.abi) \
        (shuba_contract_get android.native_cpu) \
        (shuba_contract_get android.release_optimization) \
        $shuba_fish_version \
        $shuba_jq_version \
        $shuba_bsdtar_version \
        $shuba_android_command_line_tools_version \
        (shuba_contract_get android.command_line_tools_versions)
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/android-native-policy.fish
shuba_check_release_contract_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
