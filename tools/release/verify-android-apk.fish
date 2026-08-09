#!/usr/bin/fish --no-config

function shuba_verify_apk_usage
    printf '%s\n' \
        'Usage: tools/release/verify-android-apk.fish APK' \
        '' \
        'Verify a final-named Android APK using the public release contract.'
end

function shuba_verify_apk_main
    if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_verify_apk_usage
        return 0
    end
    if test (count $argv) -ne 1
        shuba_verify_apk_usage >&2
        shuba_fail 'exactly one APK path is required'
        return 1
    end
    set --local shuba_project_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_resolve_android_verification_tools; or return 1
    shuba_validate_android_apk $shuba_project_root $argv[1] final
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/apk-validation.fish

shuba_verify_apk_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
