#!/usr/bin/fish --no-config

function shuba_probe_test_fail
    printf 'R12F upgrade-probe tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_probe_test_require --argument-names shuba_file shuba_fragment shuba_description
    grep --fixed-strings --quiet -- $shuba_fragment $shuba_file; or begin
        shuba_probe_test_fail "$shuba_description: $shuba_fragment"
        return 1
    end
end

function shuba_probe_test_main
    set --local shuba_script_directory (status dirname)
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    set --local shuba_builder $shuba_project_root/tools/release/build-android-upgrade-probe.fish
    set --local shuba_final_builder $shuba_project_root/tools/release/build-android-release.fish
    set --local shuba_init $shuba_project_root/tools/release/android-upgrade-probe.init.gradle
    set --local shuba_verifier $shuba_project_root/tools/release/verify-android-upgrade-probe.fish

    for shuba_file in $shuba_builder $shuba_final_builder $shuba_verifier
        fish --no-config --no-execute $shuba_file; or return 1
        fish_indent --check $shuba_file; or return 1
    end
    shuba_probe_test_require $shuba_builder 'math (shuba_contract_get app.version_code) + 1' 'probe code is not derived'; or return 1
    shuba_probe_test_require $shuba_builder 'artifact.kind=non-final-upgrade-probe' 'provenance lacks non-final kind'; or return 1
    shuba_probe_test_require $shuba_builder dist/non-final 'probe output is not isolated'; or return 1
    shuba_probe_test_require $shuba_builder 'prepare-android-release-signing.fish' 'probe bypasses signing preparer'; or return 1
    shuba_probe_test_require $shuba_builder 'android-release-signing.init.gradle' 'probe omits signing init'; or return 1
    shuba_probe_test_require $shuba_builder 'android-upgrade-probe.init.gradle' 'probe omits version init'; or return 1
    shuba_probe_test_require $shuba_builder 'verify-android-upgrade-probe.fish' 'probe omits public verifier'; or return 1
    if grep --fixed-strings --quiet -- dist/release $shuba_builder; or grep --fixed-strings --quiet -- 'generate-android.fish' $shuba_builder
        shuba_probe_test_fail 'probe builder reaches final output or regenerates authority'
        return 1
    end
    if grep --fixed-strings --quiet -- 'android-upgrade-probe.init.gradle' $shuba_final_builder; or grep --fixed-strings --quiet -- 'build-android-upgrade-probe.fish' $shuba_final_builder
        shuba_probe_test_fail 'final coordinator is coupled to the upgrade probe'
        return 1
    end
    for shuba_fragment in "project.path != ':app'" "withPlugin('com.android.application')" \
        'withBuildType(expectedBuildType)' 'withFlavor(expectedFlavorDimension, expectedFlavorName)' \
        'output.versionCode.set(upgradeProbeVersionCode)'
        shuba_probe_test_require $shuba_init $shuba_fragment 'upgrade init scoping changed'; or return 1
    end
    for shuba_forbidden in 'defaultConfig.versionCode =' buildTypes signingConfigs
        if grep --fixed-strings --quiet -- $shuba_forbidden $shuba_init
            shuba_probe_test_fail "upgrade init contains forbidden mutation: $shuba_forbidden"
            return 1
        end
    end
    shuba_probe_test_require $shuba_verifier upgrade-probe 'public verifier does not select probe policy'; or return 1

    set --local shuba_protected_before (mktemp /tmp/shuba-probe-protected-before.XXXXXX); or return 1
    set --local shuba_protected_after (mktemp /tmp/shuba-probe-protected-after.XXXXXX); or begin
        rm -f -- $shuba_protected_before
        return 1
    end
    sha256sum $shuba_project_root/dist/release/* | sort >$shuba_protected_before
    env -u SHUBA_ANDROID_KEYSTORE_FILE -u SHUBA_ANDROID_KEY_ALIAS \
        -u SHUBA_ANDROID_STORE_PASSWORD -u SHUBA_ANDROID_KEY_PASSWORD \
        $shuba_builder >/dev/null 2>&1
    if test $status -eq 0
        shuba_probe_test_fail 'probe coordinator accepted absent signing inputs'
        return 1
    end
    sha256sum $shuba_project_root/dist/release/* | sort >$shuba_protected_after
    if not cmp --silent $shuba_protected_before $shuba_protected_after
        rm -f -- $shuba_protected_before $shuba_protected_after
        shuba_probe_test_fail 'probe negative path changed protected final bytes'
        return 1
    end
    rm -f -- $shuba_protected_before $shuba_protected_after
    printf '%s\n' 'R12F upgrade-probe public-CLI, derived identity, Gradle scope, isolation, signing, and final-preservation probes: passed'
end

shuba_probe_test_main
set --local shuba_main_status $status
exit $shuba_main_status
