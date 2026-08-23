#!/usr/bin/fish --no-config

function shuba_release_usage
    printf '%s\n' \
        'Usage: tools/release/build-android-release.fish [--help]' \
        '' \
        'Build one signed Android Release APK through the tracked JUCE, libjxl,' \
        'Gradle, verification, immutable-state, and atomic-publication workflow.' \
        'The four external SHUBA_ANDROID_* signing variables are required.'
end

function shuba_release_main
    set --local shuba_lock_held false
    if test (count $argv) -eq 1; and test $argv[1] = --lock-held
        set shuba_lock_held true
        set --erase argv[1]
    else if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_release_usage
        return 0
    end
    if test (count $argv) -ne 0
        shuba_release_usage >&2
        shuba_fail 'Android release build accepts no arguments'
        return 1
    end
    set --global shuba_release_tools_root (realpath --canonicalize-existing -- (status dirname)); or return 1
    set --global shuba_release_project_root (realpath --canonicalize-existing -- $shuba_release_tools_root/../..); or return 1
    set --global shuba_release_build_parent $shuba_release_project_root/build
    set --global shuba_release_dist_root $shuba_release_project_root/dist
    set --global shuba_release_final_directory $shuba_release_dist_root/release
    set --local shuba_android_project $shuba_release_project_root/Builds/Android
    set --local shuba_apk_output_root $shuba_android_project/app/build/outputs/apk
    set --local shuba_gradle_wrapper $shuba_android_project/gradlew
    set --local shuba_signing_preparer $shuba_release_tools_root/prepare-android-release-signing.fish
    set --local shuba_signing_init $shuba_release_tools_root/android-release-signing.init.gradle
    set --local shuba_verifier $shuba_release_tools_root/verify-android-apk.fish

    shuba_contract_load $shuba_release_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_validate_android_sdk_layout; or return 1
    shuba_resolve_jdk; or return 1
    shuba_validate_initialized_submodules $shuba_release_project_root; or return 1
    shuba_validate_owned_directory $shuba_release_build_parent $shuba_release_project_root 'owned build parent'; or return 1
    shuba_validate_owned_directory $shuba_release_dist_root $shuba_release_project_root 'ignored distribution parent'; or return 1
    for shuba_relative_path in build dist
        git -C $shuba_release_project_root check-ignore --quiet -- $shuba_relative_path; or begin
            shuba_fail "release-owned output is not ignored: $shuba_relative_path"
            return 1
        end
    end
    if test $shuba_lock_held != true
        set --local shuba_flock_path (shuba_resolve_command flock); or return 1
        set --local shuba_fish_path (realpath --canonicalize-existing -- (status fish-path)); or return 1
        $shuba_flock_path --exclusive $shuba_release_dist_root/.shuba-release.lock \
            $shuba_fish_path --no-config $shuba_release_tools_root/build-android-release.fish --lock-held
        return $status
    end
    for shuba_file in \
        $shuba_release_tools_root/build-libjxl-android.fish \
        $shuba_release_tools_root/build-projucer.fish \
        $shuba_release_tools_root/generate-android.fish \
        $shuba_release_tools_root/check-release-contract.fish \
        $shuba_release_tools_root/check-release-identity.fish \
        $shuba_release_tools_root/check-generated-android.fish \
        $shuba_signing_preparer $shuba_verifier
        shuba_require_executable_file $shuba_file; or return 1
    end
    shuba_require_regular_file $shuba_signing_init; or return 1
    $shuba_release_tools_root/check-release-contract.fish; or return 1
    $shuba_signing_preparer --check; or return 1

    set --global --export JAVA_HOME $shuba_java_home
    set --global --export ANDROID_SDK_ROOT $shuba_android_sdk_root
    set --global --export ANDROID_HOME $shuba_android_sdk_root
    set --global --export PATH $shuba_java_home/bin $PATH

    set --global shuba_release_state_root (mktemp --directory $shuba_release_build_parent/.shuba-release-state.XXXXXX); or return 1
    printf '%s\n' 'Android release build: capturing immutable build-input and repository state.'
    shuba_capture_release_state $shuba_release_state_root/initial $shuba_release_project_root; or return 1

    printf '%s\n' 'Android release build: building or validating fingerprinted static libjxl.'
    $shuba_release_tools_root/build-libjxl-android.fish; or return 1
    set --global shuba_libjxl_fingerprint (shuba_read_builder_fingerprint \
        $shuba_release_build_parent/libjxl-android-arm64/.shuba-libjxl-build.stamp libjxl); or return 1
    printf '%s\n' 'Android release build: building or validating pinned Projucer.'
    $shuba_release_tools_root/build-projucer.fish; or return 1
    set --global shuba_projucer_fingerprint (shuba_read_builder_fingerprint \
        $shuba_release_build_parent/projucer-release/.shuba-projucer-build.stamp Projucer); or return 1
    printf '%s\n' 'Android release build: regenerating Android and JUCE outputs.'
    $shuba_release_tools_root/generate-android.fish; or return 1
    $shuba_release_tools_root/check-release-identity.fish; or return 1
    $shuba_release_tools_root/check-generated-android.fish; or return 1
    shuba_require_executable_file $shuba_gradle_wrapper; or return 1

    printf '%s\n' 'Android release build: assembling the explicit signed Release variant.'
    pushd $shuba_android_project >/dev/null; or return 1
    $shuba_signing_preparer -- $shuba_gradle_wrapper --no-daemon --console=plain \
        --init-script $shuba_signing_init :app:assembleRelease_Release
    set --local shuba_gradle_status $status
    popd >/dev/null
    if test $shuba_gradle_status -ne 0
        shuba_fail "signed Gradle assembly failed with status $shuba_gradle_status"
        return $shuba_gradle_status
    end

    set --global shuba_release_tools_root $shuba_release_tools_root
    set --local shuba_generated_apk (shuba_find_android_release_apk $shuba_apk_output_root \
        (shuba_contract_get app.application_id) (shuba_contract_get app.version_code) \
        (shuba_contract_get app.version_name)); or return 1
    set --local shuba_apk_digest (shuba_sha256_file $shuba_generated_apk); or return 1
    set --local shuba_apk_size (stat --format %s -- $shuba_generated_apk)
    if not string match --regex --quiet '^[1-9][0-9]*$' -- $shuba_apk_size
        shuba_fail 'generated Release APK has an invalid size'
        return 1
    end
    shuba_assert_release_state_unchanged $shuba_release_state_root/initial \
        $shuba_release_state_root/recheck $shuba_release_project_root construction; or return 1

    set --local shuba_artifact_basename (shuba_contract_get artifact.basename)
    shuba_release_publish_internal $shuba_release_state_root $shuba_generated_apk \
        $shuba_apk_digest $shuba_apk_size $shuba_libjxl_fingerprint $shuba_projucer_fingerprint
end

function shuba_release_publish_internal
    if test (count $argv) -ne 6
        shuba_fail 'invalid internal release publication arguments'
        return 1
    end
    set --global shuba_release_state_root $argv[1]
    set --local shuba_generated_apk $argv[2]
    set --local shuba_apk_digest $argv[3]
    set --local shuba_apk_size $argv[4]
    set --global shuba_libjxl_fingerprint $argv[5]
    set --global shuba_projucer_fingerprint $argv[6]
    set --global shuba_release_tools_root (realpath --canonicalize-existing -- (status dirname)); or return 1
    set --global shuba_release_project_root (realpath --canonicalize-existing -- $shuba_release_tools_root/../..); or return 1
    set --global shuba_release_dist_root $shuba_release_project_root/dist
    set --global shuba_release_final_directory $shuba_release_dist_root/release
    set --local shuba_verifier $shuba_release_tools_root/verify-android-apk.fish
    shuba_contract_load $shuba_release_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    shuba_resolve_android_verification_tools; or return 1
    shuba_resolve_jdk; or return 1
    set --global --export ANDROID_SDK_ROOT $shuba_android_sdk_root
    set --global --export ANDROID_HOME $shuba_android_sdk_root
    set --local shuba_basename (shuba_contract_get artifact.basename)

    if test -e $shuba_release_final_directory
        shuba_validate_historical_release_packet $shuba_release_final_directory; or return 1
    end
    shuba_atomic_publication_initialize $shuba_release_dist_root $shuba_release_final_directory release; or return 1
    set --local shuba_stage (shuba_atomic_publication_stage_path); or return 1
    set --local shuba_staged_apk $shuba_stage/$shuba_basename
    cp -- $shuba_generated_apk $shuba_staged_apk; or return 1
    chmod 0644 $shuba_staged_apk; or return 1
    if test (shuba_sha256_file $shuba_staged_apk) != $shuba_apk_digest
        shuba_fail 'staged candidate bytes differ from generated Release APK'
        return 1
    end
    set --local shuba_preverification $shuba_release_state_root/pre-publication-verification.txt
    $shuba_verifier $shuba_staged_apk >$shuba_preverification; or return 1
    shuba_write_checksum_sidecar $shuba_stage/$shuba_basename.sha256 $shuba_apk_digest $shuba_basename; or return 1
    shuba_write_release_provenance $shuba_stage/$shuba_basename.provenance.txt \
        $shuba_release_project_root $shuba_generated_apk $shuba_apk_digest $shuba_apk_size \
        $shuba_release_state_root/initial $shuba_preverification; or return 1
    begin
        printf '%s\n' 'verification_evidence_schema_version=1' pre_publication_verification_begin
        cat -- $shuba_preverification
        printf '%s\n' pre_publication_verification_end
    end >$shuba_stage/$shuba_basename.verification.txt
    chmod 0644 $shuba_stage/$shuba_basename.verification.txt; or return 1
    shuba_assert_release_state_unchanged $shuba_release_state_root/initial \
        $shuba_release_state_root/pre-publication $shuba_release_project_root publication; or return 1
    shuba_atomic_publication_publish; or return 1
    if set --query SHUBA_RELEASE_TEST_FAIL_AFTER_PUBLISH; and test "$SHUBA_RELEASE_TEST_FAIL_AFTER_PUBLISH" = 1
        shuba_fail 'injected post-publication failure'
        return 97
    end
    set --local shuba_final_apk $shuba_release_final_directory/$shuba_basename
    test (shuba_sha256_file $shuba_final_apk) = $shuba_apk_digest; or return 1
    shuba_verify_checksum_sidecar $shuba_release_final_directory/$shuba_basename.sha256 $shuba_final_apk $shuba_basename; or return 1
    set --local shuba_postverification $shuba_release_state_root/post-publication-verification.txt
    $shuba_verifier $shuba_final_apk >$shuba_postverification; or return 1
    shuba_assert_release_state_unchanged $shuba_release_state_root/initial \
        $shuba_release_state_root/post-publication $shuba_release_project_root publication; or return 1
    set --local shuba_final_verification $shuba_release_final_directory/$shuba_basename.verification.txt
    begin
        printf '%s\n' 'verification_evidence_schema_version=1' pre_publication_verification_begin
        cat -- $shuba_preverification
        printf '%s\n' pre_publication_verification_end post_publication_verification_begin
        cat -- $shuba_postverification
        printf '%s\n' post_publication_verification_end
    end >$shuba_final_verification.tmp
    chmod 0644 $shuba_final_verification.tmp; or return 1
    mv --force -- $shuba_final_verification.tmp $shuba_final_verification; or return 1
    shuba_atomic_publication_commit; or return 1
    printf 'Android release build: published verified artifact %s (SHA-256 %s).\n' $shuba_final_apk $shuba_apk_digest
end

set --global shuba_release_state_root ''
function shuba_release_cleanup --argument-names shuba_status
    shuba_atomic_publication_cleanup $shuba_status
    set --local shuba_publication_status $status
    if test -n "$shuba_release_state_root"; and test -d $shuba_release_state_root
        rm -rf -- $shuba_release_state_root
    end
    return $shuba_publication_status
end
function shuba_release_interrupt_cleanup --on-signal INT
    shuba_release_cleanup 130
    exit 130
end
function shuba_release_terminate_cleanup --on-signal TERM
    shuba_release_cleanup 143
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

shuba_release_main $argv
set --local shuba_main_status $status
shuba_release_cleanup $shuba_main_status
exit $shuba_main_status
