#!/usr/bin/fish --no-config

function shuba_generated_test_fail
    printf 'R12F generated Android tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_generated_test_run --argument-names shuba_case_root
    set --global shuba_generated_test_output (shuba_validate_generated_android $shuba_case_root 2>&1)
    set --global shuba_generated_test_status $status
end

function shuba_generated_test_require_verdict --argument-names shuba_case_root shuba_expected_acceptance shuba_description
    shuba_generated_test_run $shuba_case_root
    set --local shuba_new_accepted false
    test $shuba_generated_test_status -eq 0; and set shuba_new_accepted true
    if test $shuba_new_accepted != $shuba_expected_acceptance
        shuba_generated_test_fail "generated validator produced the wrong verdict for $shuba_description: $shuba_generated_test_output"
        return 1
    end
end

function shuba_generated_test_fixture
    set --local shuba_fixture (mktemp --directory $shuba_generated_test_root/case.XXXXXX); or return 1
    cp --preserve=mode -- $shuba_project_root/Shuba.jucer $shuba_fixture/Shuba.jucer; or return 1
    mkdir -p $shuba_fixture/Source $shuba_fixture/Localization $shuba_fixture/Builds $shuba_fixture/JuceLibraryCode; or return 1
    cp -a -- $shuba_project_root/Source/. $shuba_fixture/Source/; or return 1
    cp -a -- $shuba_project_root/Localization/. $shuba_fixture/Localization/; or return 1
    mkdir -p $shuba_fixture/Builds/Android
    cp -a -- $shuba_project_root/Builds/Android/app $shuba_fixture/Builds/Android/app; or return 1
    rm -rf -- $shuba_fixture/Builds/Android/app/.cxx $shuba_fixture/Builds/Android/app/build
    for shuba_generated_path in build.gradle gradle.properties gradlew gradlew.bat local.properties settings.gradle gradle
        cp -a -- $shuba_project_root/Builds/Android/$shuba_generated_path $shuba_fixture/Builds/Android/$shuba_generated_path; or return 1
    end
    cp -a -- $shuba_project_root/JuceLibraryCode/. $shuba_fixture/JuceLibraryCode/; or return 1
    mkdir -p $shuba_fixture/release
    cp -- $shuba_project_root/release/release.properties $shuba_fixture/release/release.properties; or return 1
    printf '%s\n' $shuba_fixture
end

function shuba_generated_test_replace --argument-names shuba_path shuba_pattern shuba_replacement
    sed "s|$shuba_pattern|$shuba_replacement|" $shuba_path >$shuba_path.mutated
    and mv $shuba_path.mutated $shuba_path
end

function shuba_generated_test_main
    set --local shuba_script_directory (status dirname)
    set --global shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    set --global shuba_release_tools_root $shuba_project_root/tools/release
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    set --global shuba_generated_test_root (mktemp --directory /tmp/shuba-r12f-generated-android.XXXXXX); or return 1

    set --local shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_require_verdict $shuba_case_root true positive; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    printf '<broken\n' >$shuba_case_root/Shuba.jucer
    shuba_generated_test_require_verdict $shuba_case_root false malformed-project-XML; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Shuba.jucer 'androidTargetSDK="34"' 'androidTargetSDK="35"'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false wrong-authority-target-SDK; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    rm $shuba_case_root/Source/Core/Clock.hpp
    shuba_generated_test_require_verdict $shuba_case_root false missing-source; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Shuba.jucer '<FILE id="coreclockhpp"' '<FILE id="coreclockhpp_duplicate" name="Clock.hpp" compile="0" resource="0" file="Source/Core/Clock.hpp"/><FILE id="coreclockhpp"'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false duplicate-source; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    printf 'unexpected\n' >$shuba_case_root/Builds/Android/unexpected.txt
    shuba_generated_test_require_verdict $shuba_case_root false unexpected-generated-file; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Builds/Android/app/src/main/AndroidManifest.xml '<application' '<uses-permission android:name="android.permission.INTERNET"/><application'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false generated-permission; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    printf '<manifest\n' >$shuba_case_root/Builds/Android/app/src/main/AndroidManifest.xml
    shuba_generated_test_require_verdict $shuba_case_root false malformed-manifest-XML; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Builds/Android/app/src/main/AndroidManifest.xml 'android:exported="true"' 'android:exported="false"'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false wrong-exported-component; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Builds/Android/app/build.gradle 'compileSdk(34)' 'compileSdk(35)'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false wrong-generated-compile-SDK; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Builds/Android/app/CMakeLists.txt '\[\[jxl_dec\]\]' '[[jxl_changed]]'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false changed-native-link-order; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    sed -E 's/(ru_poSize = )[0-9]+/\11/' $shuba_case_root/JuceLibraryCode/BinaryData.h >$shuba_case_root/JuceLibraryCode/BinaryData.h.mutated; or return 1
    mv $shuba_case_root/JuceLibraryCode/BinaryData.h.mutated $shuba_case_root/JuceLibraryCode/BinaryData.h; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false stale-BinaryData; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    printf 'not png\n' >$shuba_case_root/Builds/Android/app/src/main/res/drawable-ldpi/icon.png
    shuba_generated_test_require_verdict $shuba_case_root false malformed-PNG; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    cp -- $shuba_case_root/Builds/Android/app/src/main/res/drawable-mdpi/icon.png $shuba_case_root/Builds/Android/app/src/main/res/drawable-ldpi/icon.png; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false wrong-PNG-dimensions; or return 1

    set shuba_case_root (shuba_generated_test_fixture); or return 1
    shuba_generated_test_replace $shuba_case_root/Builds/Android/local.properties '^sdk.dir=.*$' 'sdk.dir=/does/not/exist'; or return 1
    shuba_generated_test_require_verdict $shuba_case_root false invalid-SDK-path; or return 1

    printf '%s\n' 'R12F generated Android positive and focused mutation probes: passed'
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-contract.fish
source $shuba_script_directory/../lib/android-toolchain.fish
source $shuba_script_directory/../lib/generated-android-validation.fish

set --global shuba_generated_test_root ''
function shuba_generated_test_cleanup --on-event fish_exit
    if test -n "$shuba_generated_test_root"; and test -d $shuba_generated_test_root
        rm -rf -- $shuba_generated_test_root
    end
end

shuba_generated_test_main
set --local shuba_main_status $status
exit $shuba_main_status
