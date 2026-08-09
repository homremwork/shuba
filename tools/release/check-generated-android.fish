#!/usr/bin/fish --no-config

function shuba_check_generated_android_main
    if test (count $argv) -ne 0
        shuba_fail 'generated Android check accepts no arguments'
        return 1
    end
    set --local shuba_script_directory (status dirname)
    set --local shuba_root (realpath --canonicalize-existing -- $shuba_script_directory/../..); or return 1
    $shuba_script_directory/check-release-contract.fish; or return 1
    $shuba_script_directory/check-release-identity.fish; or return 1
    shuba_contract_load $shuba_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_generated_android $shuba_root
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/generated-android-validation.fish
shuba_check_generated_android_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
