#!/usr/bin/fish --no-config

function shuba_release_identity_require --argument-names shuba_path shuba_text shuba_description
    shuba_require_regular_file $shuba_path; or return 1
    grep --fixed-strings --quiet -- $shuba_text $shuba_path; or begin
        shuba_fail "$shuba_description is absent from $shuba_path"
        return 1
    end
end

function shuba_release_identity_main
    if test (count $argv) -ne 0
        shuba_fail 'release identity check accepts no arguments'
        return 1
    end
    set --local shuba_script_directory (status dirname)
    set --local shuba_root (realpath --canonicalize-existing -- $shuba_script_directory/../..); or return 1
    shuba_contract_load $shuba_root/release/release.properties; or return 1
    shuba_require_fish_version (shuba_contract_get tool.fish_min_version); or return 1
    set --local shuba_version (shuba_contract_get app.version_name)
    set --local shuba_parts (string split . -- $shuba_version)
    set --local shuba_version_hex (printf '0x%x%02x%02x' $shuba_parts[1] $shuba_parts[2] $shuba_parts[3])
    set --local shuba_project $shuba_root/Shuba.jucer
    set --local shuba_source $shuba_root/Source/Main.cpp
    set --local shuba_header $shuba_root/JuceLibraryCode/JuceHeader.h
    set --local shuba_manifest $shuba_root/Builds/Android/app/src/main/AndroidManifest.xml
    set --local shuba_cmake $shuba_root/Builds/Android/app/CMakeLists.txt
    for shuba_record in \
        "$shuba_project|version=\"$shuba_version\"|project version" \
        "$shuba_project|androidVersionCode=\""(shuba_contract_get app.version_code)"\"|Android version code" \
        "$shuba_project|name=\""(shuba_contract_get app.name)"\"|app name" \
        "$shuba_project|bundleIdentifier=\""(shuba_contract_get app.application_id)"\"|application ID" \
        "$shuba_source|return ProjectInfo::versionString;|generated-version accessor" \
        "$shuba_source|application_version().data(),|production version use" \
        "$shuba_header|versionString  = \"$shuba_version\"|generated version" \
        "$shuba_header|projectName    = \""(shuba_contract_get app.name)"\"|generated project name" \
        "$shuba_manifest|android:versionCode=\""(shuba_contract_get app.version_code)"\"|manifest version code" \
        "$shuba_manifest|android:versionName=\"$shuba_version\"|manifest version name" \
        "$shuba_cmake|JUCE_APP_VERSION=$shuba_version|native app version" \
        "$shuba_cmake|JUCE_APP_VERSION_HEX=$shuba_version_hex|native app version hex"
        set --local shuba_fields (string split --max 2 '|' -- $shuba_record)
        shuba_release_identity_require $shuba_fields[1] $shuba_fields[2] $shuba_fields[3]; or return 1
    end
    for shuba_path in $shuba_source $shuba_header $shuba_manifest $shuba_cmake
        if grep --fixed-strings --quiet -- 0.1.0 $shuba_path
            shuba_fail "$shuba_path contains forbidden release residue: 0.1.0"
            return 1
        end
    end
    printf 'release identity check: %s %s (%s), Android code %s, is consistent.\n' \
        (shuba_contract_get app.name) $shuba_version \
        (shuba_contract_get app.application_id) (shuba_contract_get app.version_code)
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
shuba_release_identity_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
