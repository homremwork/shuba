#!/usr/bin/fish --no-config

function shuba_historical_packet_test_fail
    printf 'R12F historical release-packet tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_historical_packet_test_write_packet --argument-names shuba_directory
    mkdir -p -- $shuba_directory; or return 1
    set --local shuba_basename Shuba-1.0.0-arm64-v8a.apk
    set --local shuba_apk $shuba_directory/$shuba_basename
    set --local shuba_provenance $shuba_apk.provenance.txt
    set --local shuba_verification $shuba_apk.verification.txt
    set --local shuba_certificate (shuba_contract_get signing.certificate_sha256); or return 1
    printf '%s\n' 'historical APK fixture' >$shuba_apk; or return 1
    set --local shuba_digest (shuba_sha256_file $shuba_apk); or return 1
    set --local shuba_size (stat --format %s -- $shuba_apk); or return 1
    set --local shuba_verification_line 'Android APK verification: Shuba 1.0.0 code 1 (arm64-v8a) is signed, aligned, contract-consistent, and stripped.'
    begin
        printf '%s\n' verification_evidence_schema_version=1 pre_publication_verification_begin $shuba_verification_line \
            pre_publication_verification_end post_publication_verification_begin $shuba_verification_line \
            post_publication_verification_end
    end >$shuba_verification; or return 1
    set --local shuba_preverification_digest (printf '%s\n' $shuba_verification_line | sha256sum | string split --max 1 ' ')[1]
    begin
        printf '%s\n' \
            provenance_schema_version=2 \
            created_utc=2026-08-12T08:00:37Z \
            artifact.basename=$shuba_basename \
            artifact.sha256=$shuba_digest \
            artifact.bytes=$shuba_size \
            artifact.generated_apk_sha256=$shuba_digest \
            artifact.generated_output_metadata_sha256=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
            release_contract.sha256=bbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbbb \
            release_entrypoint.sha256=cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc \
            signing.certificate_sha256=$shuba_certificate \
            app.application_id=(shuba_contract_get app.application_id) \
            app.version_name=1.0.0 \
            app.version_code=1 \
            android.min_sdk=34 \
            android.target_sdk=34 \
            android.abi=arm64-v8a \
            android.ndk_version=29.0.14206865 \
            android.build_tools_version=35.0.1 \
            android.cmake_version=3.22.1 \
            android.gradle_version=8.13 \
            android.gradle_plugin_version=8.13.2 \
            android.command_line_tools_version=20.0 \
            android.gradle_wrapper_distribution=https://services.gradle.org/distributions/gradle-8.13-all.zip \
            libjxl.fingerprint_sha256=dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd \
            projucer.fingerprint_sha256=eeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee \
            verification.pre_publish_sha256=$shuba_preverification_digest \
            release_state_begin \
            release_state_schema_version=1 \
            root_commit=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa \
            root_dirty=false \
            root_worktree_state_sha256=ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff \
            build_input_manifest_sha256=1111111111111111111111111111111111111111111111111111111111111111 \
            submodule_state_sha256=2222222222222222222222222222222222222222222222222222222222222222 \
            release_state_end \
            submodule_state_begin \
            submodule_state_schema_version=1 \
            submodule_state_end \
            build_input_manifest_begin \
            build_input_manifest_schema_version=1 \
            build_input_manifest_end
    end >$shuba_provenance; or return 1
    shuba_write_checksum_sidecar $shuba_apk.sha256 $shuba_digest $shuba_basename; or return 1
    chmod 0644 $shuba_apk $shuba_apk.sha256 $shuba_provenance $shuba_verification
end

function shuba_historical_packet_test_prepare --argument-names shuba_case
    set --local shuba_directory $shuba_historical_packet_test_root/$shuba_case
    shuba_historical_packet_test_write_packet $shuba_directory; or return 1
    printf '%s\n' $shuba_directory
end

function shuba_historical_packet_test_require_rejection --argument-names shuba_description shuba_directory shuba_expected_message
    set --local shuba_output (shuba_validate_historical_release_packet $shuba_directory 2>&1)
    set --local shuba_status $status
    if test $shuba_status -eq 0
        shuba_historical_packet_test_fail "accepted $shuba_description"
        return 1
    end
    if not string match --quiet "*$shuba_expected_message*" -- (string join \n -- $shuba_output)
        shuba_historical_packet_test_fail "reported the wrong failure for $shuba_description"
        return 1
    end
end

function shuba_validate_historical_android_apk
    set --global shuba_historical_packet_test_validation_arguments $argv
    return 0
end

function shuba_historical_packet_test_main
    set --local shuba_script_directory (status dirname)
    set --global shuba_release_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    set --local shuba_release_builder $shuba_release_project_root/tools/release/build-android-release.fish
    shuba_contract_load $shuba_release_project_root/release/release.properties; or return 1
    set --global shuba_historical_packet_test_root (mktemp --directory /tmp/shuba-r12f-historical-packet.XXXXXX); or return 1
    grep --fixed-strings --quiet -- 'source $shuba_script_directory/lib/apk-validation.fish' $shuba_release_builder; or begin
        shuba_historical_packet_test_fail 'release coordinator does not load historical APK validation'
        return 1
    end

    set --local shuba_directory (shuba_historical_packet_test_prepare valid); or return 1
    shuba_validate_historical_release_packet $shuba_directory; or return 1
    if test (count $shuba_historical_packet_test_validation_arguments) -ne 11; or test "$shuba_historical_packet_test_validation_arguments[3]" != Shuba-1.0.0-arm64-v8a.apk; or test "$shuba_historical_packet_test_validation_arguments[4]" != (shuba_contract_get app.application_id); or test "$shuba_historical_packet_test_validation_arguments[5]" != 1; or test "$shuba_historical_packet_test_validation_arguments[6]" != 1.0.0; or test "$shuba_historical_packet_test_validation_arguments[9]" != arm64-v8a; or test "$shuba_historical_packet_test_validation_arguments[11]" != Shuba
        shuba_historical_packet_test_fail 'did not reach the historical APK validator with complete evidence'
        return 1
    end

    set shuba_directory (shuba_historical_packet_test_prepare extra-file); or return 1
    printf '%s\n' forbidden >$shuba_directory/extra
    shuba_historical_packet_test_require_rejection extra-file $shuba_directory 'unexpected four-file inventory'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare symlink); or return 1
    ln -s Shuba-1.0.0-arm64-v8a.apk $shuba_directory/forbidden-link; or return 1
    shuba_historical_packet_test_require_rejection symbolic-link $shuba_directory 'contains a symbolic link'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare checksum); or return 1
    printf '%s\n' '0000000000000000000000000000000000000000000000000000000000000000  Shuba-1.0.0-arm64-v8a.apk' >$shuba_directory/Shuba-1.0.0-arm64-v8a.apk.sha256
    shuba_historical_packet_test_require_rejection checksum $shuba_directory 'checksum sidecar does not match'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare provenance-digest); or return 1
    sed -i 's/^artifact.generated_apk_sha256=.*/artifact.generated_apk_sha256=0000000000000000000000000000000000000000000000000000000000000000/' $shuba_directory/*.provenance.txt
    shuba_historical_packet_test_require_rejection provenance-digest $shuba_directory 'APK bytes differ from retained provenance'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare duplicate-provenance); or return 1
    sed -i '/^release_state_begin/i app.version_code=1' $shuba_directory/*.provenance.txt
    shuba_historical_packet_test_require_rejection duplicate-provenance $shuba_directory 'repeats record: app.version_code'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare application-id); or return 1
    sed -i 's/^app.application_id=.*/app.application_id=example.invalid/' $shuba_directory/*.provenance.txt
    shuba_historical_packet_test_require_rejection application-id $shuba_directory 'application ID differs from the successor contract'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare abi); or return 1
    sed -i 's/^android.abi=.*/android.abi=x86_64/' $shuba_directory/*.provenance.txt
    shuba_historical_packet_test_require_rejection abi $shuba_directory 'ABI differs from the successor contract'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare certificate); or return 1
    sed -i 's/^signing.certificate_sha256=.*/signing.certificate_sha256=0000000000000000000000000000000000000000000000000000000000000000/' $shuba_directory/*.provenance.txt
    shuba_historical_packet_test_require_rejection certificate $shuba_directory 'signing certificate differs from the successor contract'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare version-code); or return 1
    sed -i 's/^app.version_code=.*/app.version_code=2/' $shuba_directory/*.provenance.txt
    shuba_historical_packet_test_require_rejection version-code $shuba_directory 'version code is not lower'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare verification); or return 1
    sed -i 's/code 1/code 9/' $shuba_directory/*.verification.txt
    shuba_historical_packet_test_require_rejection verification $shuba_directory 'verification evidence does not bind'; or return 1

    set shuba_directory (shuba_historical_packet_test_prepare probe); or return 1
    for shuba_path in $shuba_directory/Shuba-1.0.0-arm64-v8a.apk*
        mv $shuba_path (string replace Shuba-1.0.0-arm64-v8a Shuba-1.0.0-arm64-v8a-upgrade -- $shuba_path); or return 1
    end
    sed -i 's/Shuba-1.0.0-arm64-v8a.apk/Shuba-1.0.0-arm64-v8a-upgrade.apk/g' $shuba_directory/*.provenance.txt $shuba_directory/*.sha256
    shuba_historical_packet_test_require_rejection upgrade-probe $shuba_directory 'must not be an upgrade probe'; or return 1

    printf '%s\n' 'R12F historical packet provenance, successor continuity, and mutation probes: passed'
end

set --global shuba_historical_packet_test_root ''
function shuba_historical_packet_test_cleanup --on-event fish_exit
    if test -n "$shuba_historical_packet_test_root"; and test -d $shuba_historical_packet_test_root
        rm -rf -- $shuba_historical_packet_test_root
    end
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-contract.fish
source $shuba_script_directory/../lib/release-artifact.fish

shuba_historical_packet_test_main
set --local shuba_main_status $status
exit $shuba_main_status
