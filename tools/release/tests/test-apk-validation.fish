#!/usr/bin/fish --no-config

function shuba_apk_test_fail
    printf 'R12F APK validation tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_apk_test_require_rejection --argument-names shuba_description
    set --erase argv[1]
    $argv >/dev/null 2>&1
    if test $status -eq 0
        shuba_apk_test_fail "accepted $shuba_description"
        return 1
    end
end

function shuba_apk_test_write_executable --argument-names shuba_path shuba_body
    printf '%s\n' '#!/bin/sh' $shuba_body >$shuba_path; or return 1
    chmod 0700 $shuba_path
end

function shuba_apk_test_write_manifest --argument-names shuba_path shuba_extra_application
    set --local shuba_application_open '  <application android:label="@ref/0x7f020000"'
    if test -n "$shuba_extra_application"
        set shuba_application_open "$shuba_application_open $shuba_extra_application"
    end
    set shuba_application_open "$shuba_application_open>"
    printf '%s\n' \
        '<?xml version="1.0" encoding="utf-8"?>' \
        '<manifest xmlns:android="http://schemas.android.com/apk/res/android" package="'(shuba_contract_get app.application_id)'" android:versionCode="'(shuba_contract_get app.version_code)'" android:versionName="'(shuba_contract_get app.version_name)'">' \
        '  <uses-sdk android:minSdkVersion="'(shuba_contract_get android.min_sdk)'" android:targetSdkVersion="'(shuba_contract_get android.target_sdk)'"/>' \
        $shuba_application_open \
        '    <receiver android:name="com.rmsl.juce.Receiver" android:exported="false"/>' \
        '    <activity android:name="android.app.Activity" android:exported="true">' \
        '      <intent-filter><action android:name="android.intent.action.MAIN"/><category android:name="android.intent.category.LAUNCHER"/></intent-filter>' \
        '    </activity>' \
        '  </application>' \
        '</manifest>' >$shuba_path
end

function shuba_apk_test_use_fakes --argument-names shuba_manifest shuba_apksigner_mode shuba_readelf_mode shuba_config_mode
    set --global shuba_apkanalyzer_path $shuba_apk_test_root/apkanalyzer
    set --global shuba_aapt2_path $shuba_apk_test_root/aapt2
    set --global shuba_apksigner_path $shuba_apk_test_root/apksigner
    set --global shuba_zipalign_path $shuba_apk_test_root/zipalign
    set --global shuba_ndk_readelf_path $shuba_apk_test_root/readelf
    set --global --export SHUBA_APK_TEST_MANIFEST $shuba_manifest
    set --global --export SHUBA_APK_TEST_APKSIGNER_MODE $shuba_apksigner_mode
    set --global --export SHUBA_APK_TEST_READELF_MODE $shuba_readelf_mode
    set --global --export SHUBA_APK_TEST_CONFIG_MODE $shuba_config_mode
end

function shuba_apk_test_prepare_fakes
    shuba_apk_test_write_executable $shuba_apk_test_root/apkanalyzer \
        'if [ "$1 $2" = "manifest print" ]; then cat "$SHUBA_APK_TEST_MANIFEST"; exit 0; fi
if [ "$1 $2" = "resources configs" ]; then
    if [ "$SHUBA_APK_TEST_CONFIG_MODE" = missing ]; then printf "%s\n" ldpi mdpi hdpi xhdpi; else printf "%s\n" default ldpi mdpi hdpi xhdpi anydpi; fi
    exit 0
fi
exit 1'; or return 1
    shuba_apk_test_write_executable $shuba_apk_test_root/aapt2 \
        'if [ "$1 $2" = "dump badging" ]; then printf "application-label:'"'"'Shuba'"'"'\n"; exit 0; fi
if [ "$1 $2" = "dump resources" ]; then printf "%s\n" "    resource 0x7f010000 drawable/icon"; exit 0; fi
exit 1'; or return 1
    shuba_apk_test_write_executable $shuba_apk_test_root/apksigner \
        'if [ "$SHUBA_APK_TEST_APKSIGNER_MODE" = wrong ]; then digest=aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa; else digest=1ba3e5bf7a0a59407407c2ca10f6e8a42654f5303fe218fc610eb4eac7fde861; fi
printf "%s\n" "Verified using v2 scheme (APK Signature Scheme v2): true" "Number of signers: 1" "Signer #1 certificate SHA-256 digest: $digest"'; or return 1
    shuba_apk_test_write_executable $shuba_apk_test_root/zipalign 'printf "%s\n" "Verification successful"'; or return 1
    shuba_apk_test_write_executable $shuba_apk_test_root/readelf \
        'if [ "$1" = "--file-header" ]; then
    if [ "$SHUBA_APK_TEST_READELF_MODE" = wrong-machine ]; then printf "%s\n" "  Machine: X86-64"; else printf "%s\n" "  Machine: AArch64"; fi
else
    printf "%s\n" "Section Headers:"
    if [ "$SHUBA_APK_TEST_READELF_MODE" = unstripped ]; then printf "%s\n" "  [ 1] .symtab SYMTAB"; fi
fi'; or return 1
end

function shuba_apk_test_make_archive --argument-names shuba_archive shuba_native_path shuba_link_path
    set --local shuba_source_root (mktemp --directory $shuba_apk_test_root/archive.XXXXXX); or return 1
    mkdir -p $shuba_source_root/(path dirname $shuba_native_path); or return 1
    printf 'elf\n' >$shuba_source_root/$shuba_native_path
    printf 'manifest\n' >$shuba_source_root/AndroidManifest.xml
    printf 'resources\n' >$shuba_source_root/resources.arsc
    if test -n "$shuba_link_path"
        mkdir -p $shuba_source_root/(path dirname $shuba_link_path); or return 1
        ln -s ../AndroidManifest.xml $shuba_source_root/$shuba_link_path; or return 1
    end
    $shuba_bsdtar_path --create --file $shuba_archive --directory $shuba_source_root .; or return 1
end

function shuba_apk_test_main
    set --local shuba_with_real false
    if test (count $argv) -eq 1; and test $argv[1] = --with-real-apk
        set shuba_with_real true
    else if test (count $argv) -ne 0
        shuba_apk_test_fail 'usage: test-apk-validation.fish [--with-real-apk]'
        return 1
    end
    set --local shuba_script_directory (status dirname)
    set --global shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    set --global shuba_apk_test_root (mktemp --directory /tmp/shuba-r12f-apk-tests.XXXXXX); or return 1

    set --local shuba_valid_manifest $shuba_apk_test_root/valid-manifest.xml
    shuba_apk_test_write_manifest $shuba_valid_manifest ''; or return 1

    if test $shuba_with_real = true
        set --local shuba_real_apk $shuba_project_root/dist/release/(shuba_contract_get artifact.basename)
        shuba_validate_android_toolchain; or return 1
        shuba_resolve_android_verification_tools; or return 1
        shuba_validate_android_apk $shuba_project_root $shuba_real_apk final >/dev/null; or return 1
    end

    shuba_apk_test_prepare_fakes; or return 1
    shuba_apk_test_use_fakes $shuba_valid_manifest valid valid valid
    set --local shuba_fixture_apk $shuba_apk_test_root/(shuba_contract_get artifact.basename)
    shuba_apk_test_make_archive $shuba_fixture_apk (shuba_contract_get artifact.native_library_path) ''; or return 1
    shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final >/dev/null; or return 1

    shuba_apk_test_use_fakes $shuba_valid_manifest wrong valid valid
    shuba_apk_test_require_rejection wrong-signer shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1

    set --local shuba_debug_manifest $shuba_apk_test_root/debug-manifest.xml
    shuba_apk_test_write_manifest $shuba_debug_manifest 'android:debuggable="true"'; or return 1
    shuba_apk_test_use_fakes $shuba_debug_manifest valid valid valid
    shuba_apk_test_require_rejection debuggable-manifest shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1

    shuba_apk_test_use_fakes $shuba_valid_manifest valid wrong-machine valid
    shuba_apk_test_require_rejection wrong-ELF-machine shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1
    shuba_apk_test_use_fakes $shuba_valid_manifest valid unstripped valid
    shuba_apk_test_require_rejection unstripped-ELF shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1
    shuba_apk_test_use_fakes $shuba_valid_manifest valid valid missing
    shuba_apk_test_require_rejection missing-icon-qualifier shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1

    set --local shuba_wrong_abi $shuba_apk_test_root/wrong-abi.apk
    shuba_apk_test_make_archive $shuba_wrong_abi lib/x86_64/libjuce_jni.so ''; or return 1
    cp -- $shuba_wrong_abi $shuba_fixture_apk
    shuba_apk_test_use_fakes $shuba_valid_manifest valid valid valid
    shuba_apk_test_require_rejection wrong-ABI-payload shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1

    set --local shuba_link_archive $shuba_apk_test_root/link.apk
    shuba_apk_test_make_archive $shuba_link_archive (shuba_contract_get artifact.native_library_path) res/link; or return 1
    cp -- $shuba_link_archive $shuba_fixture_apk
    shuba_apk_test_require_rejection archive-link shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk final; or return 1

    shuba_apk_test_require_rejection final-wrong-filename shuba_validate_android_apk $shuba_project_root $shuba_link_archive final; or return 1
    shuba_apk_test_require_rejection probe-wrong-location shuba_validate_android_apk $shuba_project_root $shuba_fixture_apk upgrade-probe; or return 1

    printf '%s\n' 'R12F APK verifier positive and focused mutation probes: passed'
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-contract.fish
source $shuba_script_directory/../lib/android-toolchain.fish
source $shuba_script_directory/../lib/apk-validation.fish

set --global shuba_apk_test_root ''
function shuba_apk_test_cleanup --on-event fish_exit
    if test -n "$shuba_apk_test_root"; and test -d $shuba_apk_test_root
        rm -rf -- $shuba_apk_test_root
    end
end

shuba_apk_test_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
