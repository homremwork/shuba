#!/usr/bin/fish --no-config

function shuba_probe_usage
    printf '%s\n' \
        'Usage: tools/release/build-android-upgrade-probe.fish [--help]' \
        '' \
        'Build one signed, non-final, same-key code-2 APK. The command never' \
        'regenerates authority and publishes only under dist/non-final.'
end

function shuba_probe_write_provenance --argument-names shuba_path shuba_generated_apk shuba_digest shuba_size shuba_verification
    set --local shuba_tools $shuba_path.tools
    shuba_write_structured_tool_descriptor $shuba_tools; or return 1
    begin
        printf '%s\n' 'provenance_schema_version=2' 'artifact.kind=non-final-upgrade-probe'
        printf 'created_utc=%s\n' (date --utc +%Y-%m-%dT%H:%M:%SZ)
        printf 'artifact.basename=%s\nartifact.sha256=%s\nartifact.bytes=%s\n' \
            $shuba_probe_basename $shuba_digest $shuba_size
        printf 'release_contract.sha256=%s\n' (shuba_sha256_file $shuba_probe_project_root/release/release.properties)
        printf 'upgrade_probe_entrypoint.sha256=%s\n' (shuba_sha256_file $shuba_probe_tools_root/build-android-upgrade-probe.fish)
        printf 'upgrade_probe_init_script.sha256=%s\n' (shuba_sha256_file $shuba_probe_init)
        printf 'signing.certificate_sha256=%s\n' (shuba_contract_get signing.certificate_sha256)
        printf 'android.application_id=%s\nandroid.version_name=%s\n' \
            (shuba_contract_get app.application_id) (shuba_contract_get app.version_name)
        printf 'android.source_version_code=%s\nandroid.upgrade_probe_version_code=%s\n' \
            (shuba_contract_get app.version_code) $shuba_probe_version_code
        for shuba_key in android.min_sdk android.target_sdk android.abi android.ndk_version \
            android.build_tools_version android.cmake_version android.gradle_version \
            android.gradle_plugin_version android.command_line_tools_version
            printf '%s=%s\n' $shuba_key (shuba_contract_get $shuba_key)
        end
        printf 'tool.java.home=%s\ntool.java.version=%s\ntool.java.sha256=%s\n' \
            $shuba_java_home $shuba_java_version_output (shuba_sha256_file $shuba_java_path)
        printf 'tool.javac.version=%s\ntool.javac.sha256=%s\n' $shuba_javac_version (shuba_sha256_file $shuba_javac_path)
        printf 'artifact.generated_apk_sha256=%s\nartifact.generated_output_metadata_sha256=%s\n' \
            $shuba_digest (shuba_sha256_file (path dirname $shuba_generated_apk)/output-metadata.json)
        printf 'verification.sha256=%s\n' (shuba_sha256_file $shuba_verification)
        cat -- $shuba_tools
        printf '%s\n' release_state_begin
        cat -- $shuba_probe_state_root/initial/release.state
        printf '%s\n' release_state_end submodule_state_begin
        cat -- $shuba_probe_state_root/initial/submodules.state
        printf '%s\n' submodule_state_end build_input_manifest_begin
        cat -- $shuba_probe_state_root/initial/build-inputs.manifest
        printf '%s\n' build_input_manifest_end
    end >$shuba_path
    set --local shuba_status $status
    rm -f -- $shuba_tools
    if test $shuba_status -eq 0
        chmod 0644 $shuba_path
    end
    return $shuba_status
end

function shuba_probe_main
    set --local shuba_lock_held false
    if test (count $argv) -eq 1; and test $argv[1] = --lock-held
        set shuba_lock_held true
        set --erase argv[1]
    else if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_probe_usage
        return 0
    end
    if test (count $argv) -ne 0
        shuba_probe_usage >&2
        shuba_fail 'Android upgrade-probe build accepts no arguments'
        return 1
    end
    set --global shuba_probe_tools_root (realpath --canonicalize-existing -- (status dirname)); or return 1
    set --global shuba_probe_project_root (realpath --canonicalize-existing -- $shuba_probe_tools_root/../..); or return 1
    set --global shuba_probe_dist_root $shuba_probe_project_root/dist
    set --global shuba_probe_non_final_root $shuba_probe_dist_root/non-final
    set --local shuba_android_project $shuba_probe_project_root/Builds/Android
    set --local shuba_apk_output_root $shuba_android_project/app/build/outputs/apk
    set --local shuba_gradle_wrapper $shuba_android_project/gradlew
    set --local shuba_signing_preparer $shuba_probe_tools_root/prepare-android-release-signing.fish
    set --local shuba_signing_init $shuba_probe_tools_root/android-release-signing.init.gradle
    set --global shuba_probe_init $shuba_probe_tools_root/android-upgrade-probe.init.gradle
    set --local shuba_verifier $shuba_probe_tools_root/verify-android-upgrade-probe.fish

    shuba_contract_load $shuba_probe_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_validate_android_sdk_layout; or return 1
    shuba_resolve_jdk; or return 1
    shuba_validate_initialized_submodules $shuba_probe_project_root; or return 1
    shuba_validate_owned_directory $shuba_probe_dist_root $shuba_probe_project_root 'ignored distribution parent'; or return 1
    shuba_validate_owned_directory $shuba_probe_non_final_root $shuba_probe_dist_root 'ignored non-final parent'; or return 1
    for shuba_relative_path in dist dist/non-final
        git -C $shuba_probe_project_root check-ignore --quiet -- $shuba_relative_path; or return 1
    end
    if test $shuba_lock_held != true
        set --local shuba_flock_path (shuba_resolve_command flock); or return 1
        set --local shuba_fish_path (realpath --canonicalize-existing -- (status fish-path)); or return 1
        $shuba_flock_path --exclusive $shuba_probe_non_final_root/.shuba-upgrade-probe.lock \
            $shuba_fish_path --no-config $shuba_probe_tools_root/build-android-upgrade-probe.fish --lock-held
        return $status
    end
    for shuba_file in $shuba_probe_tools_root/check-release-contract.fish \
        $shuba_probe_tools_root/check-release-identity.fish $shuba_probe_tools_root/check-generated-android.fish \
        $shuba_signing_preparer $shuba_verifier
        shuba_require_executable_file $shuba_file; or return 1
    end
    for shuba_file in $shuba_signing_init $shuba_probe_init
        shuba_require_regular_file $shuba_file; or return 1
    end
    $shuba_probe_tools_root/check-release-contract.fish; or return 1
    $shuba_probe_tools_root/check-release-identity.fish; or return 1
    $shuba_probe_tools_root/check-generated-android.fish; or return 1
    $shuba_signing_preparer --check; or return 1
    shuba_require_executable_file $shuba_gradle_wrapper; or return 1

    set --global shuba_probe_version_code (math (shuba_contract_get app.version_code) + 1)
    set --global shuba_probe_basename (string replace --regex '[.]apk$' \
        "-upgrade-code-$shuba_probe_version_code.apk" -- (shuba_contract_get artifact.basename))
    set --global shuba_probe_destination $shuba_probe_non_final_root/upgrade-probe-code-$shuba_probe_version_code
    set --global --export JAVA_HOME $shuba_java_home
    set --global --export ANDROID_SDK_ROOT $shuba_android_sdk_root
    set --global --export ANDROID_HOME $shuba_android_sdk_root
    set --global --export PATH $shuba_java_home/bin $PATH
    set --global shuba_probe_state_root (mktemp --directory $shuba_probe_project_root/build/.shuba-upgrade-probe-state.XXXXXX); or return 1
    shuba_capture_release_state $shuba_probe_state_root/initial $shuba_probe_project_root; or return 1
    set --local --export ORG_GRADLE_PROJECT_shubaUpgradeProbeApplicationId (shuba_contract_get app.application_id)
    set --local --export ORG_GRADLE_PROJECT_shubaUpgradeProbeVersionCode $shuba_probe_version_code
    printf '%s\n' 'Android upgrade-probe build: assembling the isolated signed code-2 Release variant.'
    pushd $shuba_android_project >/dev/null; or return 1
    $shuba_signing_preparer -- $shuba_gradle_wrapper --no-daemon --console=plain \
        --init-script $shuba_signing_init --init-script $shuba_probe_init :app:assembleRelease_Release
    set --local shuba_gradle_status $status
    popd >/dev/null
    if test $shuba_gradle_status -ne 0
        return $shuba_gradle_status
    end
    set --global shuba_release_tools_root $shuba_probe_tools_root
    set --local shuba_generated_apk (shuba_find_android_release_apk $shuba_apk_output_root \
        (shuba_contract_get app.application_id) $shuba_probe_version_code (shuba_contract_get app.version_name)); or return 1
    set --local shuba_digest (shuba_sha256_file $shuba_generated_apk); or return 1
    set --local shuba_size (stat --format %s -- $shuba_generated_apk)
    string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_size; or return 1
    shuba_assert_release_state_unchanged $shuba_probe_state_root/initial \
        $shuba_probe_state_root/post-construction $shuba_probe_project_root construction; or return 1
    if test -e $shuba_probe_destination
        shuba_validate_artifact_directory $shuba_probe_destination $shuba_probe_basename $shuba_verifier; or return 1
    end
    shuba_atomic_publication_initialize $shuba_probe_non_final_root $shuba_probe_destination upgrade-probe; or return 1
    set --local shuba_stage (shuba_atomic_publication_stage_path); or return 1
    cp -- $shuba_generated_apk $shuba_stage/$shuba_probe_basename; or return 1
    chmod 0644 $shuba_stage/$shuba_probe_basename; or return 1
    test (shuba_sha256_file $shuba_stage/$shuba_probe_basename) = $shuba_digest; or return 1
    $shuba_verifier $shuba_stage/$shuba_probe_basename >$shuba_stage/$shuba_probe_basename.verification.txt; or return 1
    chmod 0644 $shuba_stage/$shuba_probe_basename.verification.txt; or return 1
    shuba_write_checksum_sidecar $shuba_stage/$shuba_probe_basename.sha256 $shuba_digest $shuba_probe_basename; or return 1
    shuba_probe_write_provenance $shuba_stage/$shuba_probe_basename.provenance.txt \
        $shuba_generated_apk $shuba_digest $shuba_size $shuba_stage/$shuba_probe_basename.verification.txt; or return 1
    shuba_assert_release_state_unchanged $shuba_probe_state_root/initial \
        $shuba_probe_state_root/pre-publication $shuba_probe_project_root publication; or return 1
    shuba_atomic_publication_publish; or return 1
    if set --query SHUBA_PROBE_TEST_FAIL_AFTER_PUBLISH; and test "$SHUBA_PROBE_TEST_FAIL_AFTER_PUBLISH" = 1
        shuba_fail 'injected upgrade-probe post-publication failure'
        return 97
    end
    shuba_validate_artifact_directory $shuba_probe_destination $shuba_probe_basename $shuba_verifier; or return 1
    shuba_assert_release_state_unchanged $shuba_probe_state_root/initial \
        $shuba_probe_state_root/post-publication $shuba_probe_project_root publication; or return 1
    shuba_atomic_publication_commit; or return 1
    printf 'Android upgrade-probe build: published non-final artifact %s (SHA-256 %s).\n' \
        $shuba_probe_destination/$shuba_probe_basename $shuba_digest
end

function shuba_probe_cleanup --argument-names shuba_status
    shuba_atomic_publication_cleanup $shuba_status
    set --local shuba_publication_status $status
    if set --query shuba_probe_state_root; and test -n "$shuba_probe_state_root"; and test -d $shuba_probe_state_root
        rm -rf -- $shuba_probe_state_root
    end
    return $shuba_publication_status
end
function shuba_probe_interrupt_cleanup --on-signal INT
    shuba_probe_cleanup 130
    exit 130
end
function shuba_probe_terminate_cleanup --on-signal TERM
    shuba_probe_cleanup 143
    exit 143
end

umask 022
set --global --export LC_ALL C
set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/repository-state.fish
source $shuba_script_directory/lib/fingerprint.fish
source $shuba_script_directory/lib/agp-metadata.fish
source $shuba_script_directory/lib/atomic-publication.fish
source $shuba_script_directory/lib/release-artifact.fish

shuba_probe_main $argv
set --local shuba_main_status $status
shuba_probe_cleanup $shuba_main_status
exit $shuba_main_status
