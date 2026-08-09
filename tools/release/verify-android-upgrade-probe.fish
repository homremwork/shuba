#!/usr/bin/fish --no-config

function shuba_verify_probe_main
    if test (count $argv) -eq 1; and test $argv[1] = --help
        printf '%s\n' 'Usage: tools/release/verify-android-upgrade-probe.fish APK'
        return 0
    end
    if test (count $argv) -ne 1
        shuba_fail 'exactly one upgrade-probe APK path is required'
        return 1
    end
    set --local shuba_project_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_resolve_android_verification_tools; or return 1
    shuba_validate_android_apk $shuba_project_root $argv[1] upgrade-probe
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/apk-validation.fish

shuba_verify_probe_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
