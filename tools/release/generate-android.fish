#!/usr/bin/fish --no-config

function shuba_generate_android_usage
    printf '%s\n' \
        'Usage: tools/release/generate-android.fish [--help]' \
        '' \
        'Build or reuse the pinned Projucer, remove only owned generated outputs,' \
        'resave Shuba.jucer in an isolated home, and validate generated output.'
end

function shuba_generate_android_main
    if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_generate_android_usage
        return 0
    end
    if test (count $argv) -ne 0
        shuba_generate_android_usage >&2
        shuba_fail 'Android generation accepts no arguments'
        return 1
    end
    set --local shuba_tools_root (status dirname)
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_tools_root/../..); or return 1
    set --local shuba_build_parent $shuba_project_root/build
    set --local shuba_project_file $shuba_project_root/Shuba.jucer
    set --local shuba_android_output $shuba_project_root/Builds/Android
    set --local shuba_juce_output $shuba_project_root/JuceLibraryCode
    set --local shuba_projucer_home $shuba_build_parent/projucer-release-home
    set --local shuba_projucer $shuba_build_parent/projucer-release/extras/Projucer/Projucer_artefacts/Release/Projucer
    set --local shuba_lock_file $shuba_build_parent/.shuba-android-generation.lock

    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    shuba_require_regular_file $shuba_project_file; or return 1
    shuba_validate_owned_directory $shuba_build_parent $shuba_project_root 'owned build parent'; or return 1
    set --global shuba_android_sdk_root (shuba_resolve_android_sdk_root); or return 1
    shuba_validate_android_sdk_layout; or return 1
    shuba_resolve_jdk; or return 1
    $shuba_tools_root/build-projucer.fish; or return 1
    shuba_require_executable_file $shuba_projucer; or return 1

    for shuba_output in $shuba_android_output $shuba_juce_output
        if test -L $shuba_output
            shuba_fail "owned generated output must not be a symbolic link: $shuba_output"
            return 1
        end
        if test -e $shuba_output
            git -C $shuba_project_root check-ignore --quiet -- (string replace "$shuba_project_root/" '' -- $shuba_output); or begin
                shuba_fail "refusing to remove generated output not covered by .gitignore: $shuba_output"
                return 1
            end
        end
    end

    set --local shuba_flock_path (shuba_resolve_command flock); or return 1
    set --local shuba_project_hash_before \
        (shuba_project_authority_canonical_sha256 $shuba_project_file); or return 1
    $shuba_flock_path --exclusive $shuba_lock_file $shuba_fish_path --no-config -c \
        'set -l tools_root $argv[1]
         set -l project_root $argv[2]
         set -l project_file $argv[3]
         set -l android_output $argv[4]
         set -l juce_output $argv[5]
         set -l projucer_home $argv[6]
         set -l projucer $argv[7]
         set -l sdk_root $argv[8]
         set -l java_home $argv[9]
         rm -rf -- $android_output $juce_output $projucer_home; or exit 1
         mkdir -p -- $projucer_home; or exit 1
         set -lx HOME $projucer_home
         set -e XDG_CONFIG_HOME
         set -lx ANDROID_SDK_ROOT $sdk_root
         set -lx ANDROID_HOME $sdk_root
         set -lx JAVA_HOME $java_home
         $projucer --help >/dev/null; or exit 1
         $projucer --set-global-search-path linux androidSDKPath $sdk_root; or exit 1
         printf "Android generation: resaving %s with pinned Projucer\n" $project_file
         $projucer --resave $project_file --lf; or exit 1
         $tools_root/check-generated-android.fish' \
        $shuba_tools_root $shuba_project_root $shuba_project_file $shuba_android_output \
        $shuba_juce_output $shuba_projucer_home $shuba_projucer $shuba_android_sdk_root $shuba_java_home
    set --local shuba_generation_status $status
    if test $shuba_generation_status -ne 0
        shuba_fail 'Android generation failed'
        return $shuba_generation_status
    end
    set --local shuba_project_hash_after \
        (shuba_project_authority_canonical_sha256 $shuba_project_file); or return 1
    if test "$shuba_project_hash_after" != "$shuba_project_hash_before"
        shuba_fail 'Projucer resave changed tracked Shuba.jucer authority'
        return 1
    end
    printf '%s\n' 'Android generation: generated outputs passed all assertions.'
end

umask 022
set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/android-toolchain.fish
source $shuba_script_directory/lib/project-authority.fish

shuba_generate_android_main $argv
set --local shuba_main_status $status
exit $shuba_main_status
