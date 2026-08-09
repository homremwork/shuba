#!/usr/bin/fish --no-config

function shuba_projucer_usage
    printf '%s\n' \
        'Usage: tools/release/build-projucer.fish [OPTION]' \
        '' \
        'Options:' \
        '  --clean               recreate only build/projucer-release' \
        '  --check               verify the current cache without mutation' \
        '  --print-path          print the validated Projucer path' \
        '  --print-fingerprint   print the current fingerprint' \
        '  --help                show this help'
end

function shuba_projucer_validate_source --argument-names shuba_project_root shuba_source_root
    set --local shuba_expected_commit 7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2
    set --local shuba_submodule_line ($shuba_git_path -C $shuba_project_root submodule status -- third_party/JUCE); or return 1
    if not string match --regex --quiet '^ 7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2 third_party/JUCE([ (]|$)' -- $shuba_submodule_line
        shuba_fail 'JUCE is absent, conflicted, dirty at the gitlink level, or not at the required commit'
        return 1
    end
    test ($shuba_git_path -C $shuba_source_root rev-parse HEAD) = $shuba_expected_commit; or begin
        shuba_fail 'JUCE HEAD differs from the required release commit'
        return 1
    end
    test ($shuba_git_path -C $shuba_source_root rev-list -n 1 '8.0.13^{commit}') = $shuba_expected_commit; or begin
        shuba_fail 'JUCE 8.0.13 tag differs from the required release commit'
        return 1
    end
end

function shuba_projucer_validate_outputs --argument-names shuba_source_root shuba_build_root shuba_executable
    set --local shuba_cache $shuba_build_root/CMakeCache.txt
    shuba_require_regular_file $shuba_cache; or return 1
    for shuba_record in \
        'CMAKE_BUILD_TYPE|Release' 'JUCE_BUILD_EXAMPLES|OFF' 'JUCE_BUILD_EXTRAS|ON' \
        "CMAKE_GENERATOR|Ninja" "CMAKE_HOME_DIRECTORY|$shuba_source_root" \
        "CMAKE_C_COMPILER|$shuba_c_compiler" "CMAKE_CXX_COMPILER|$shuba_cxx_compiler"
        set --local shuba_fields (string split --max 1 '|' -- $shuba_record)
        set --local shuba_lines (grep --extended-regexp "^$shuba_fields[1]:[^=]+=" $shuba_cache)
        if test $status -ne 0; or test (count $shuba_lines) -ne 1; or test (string replace --regex '^[^=]+=' '' -- $shuba_lines[1]) != "$shuba_fields[2]"
            shuba_fail "Projucer CMake cache $shuba_fields[1] differs from the expected value"
            return 1
        end
    end
    shuba_require_executable_file $shuba_executable; or return 1
    $shuba_executable --help | string match --quiet '*Projucer --resave project_file*'; or begin
        shuba_fail 'Projucer does not expose the expected resave command'
        return 1
    end
    set --local shuba_version ($shuba_executable --get-version $shuba_source_root/extras/Projucer/Projucer.jucer); or return 1
    test "$shuba_version" = 8.0.13; or begin
        shuba_fail 'Projucer executable reports an unexpected version'
        return 1
    end
end

function shuba_projucer_write_descriptor --argument-names shuba_project_root shuba_source_root shuba_output
    set --local shuba_tool_descriptor $shuba_output.tools
    set --local shuba_source_manifest $shuba_output.sources
    shuba_write_structured_tool_descriptor $shuba_tool_descriptor; or return 1
    shuba_write_tooling_source_manifest $shuba_source_manifest $shuba_project_root \
        $shuba_project_root/tools/release/build-projucer.fish \
        $shuba_project_root/tools/release/lib/core.fish \
        $shuba_project_root/tools/release/lib/release-contract.fish \
        $shuba_project_root/tools/release/lib/android-toolchain.fish \
        $shuba_project_root/tools/release/lib/repository-state.fish \
        $shuba_project_root/tools/release/lib/fingerprint.fish; or return 1
    begin
        printf '%s\n' 'fingerprint_schema_version=2' 'juce_release_version=8.0.13' \
            'juce_release_commit=7c9d3783b127263d72bb65fe0a7e2dc8a02a7ac2'
        printf 'juce_state_sha256=%s\n' (shuba_repository_state_digest $shuba_source_root)
        printf 'cmake.path=%s\ncmake.version=%s\ncmake.sha256=%s\n' $shuba_cmake_path $shuba_cmake_version (shuba_sha256_file $shuba_cmake_path)
        printf 'ninja.path=%s\nninja.version=%s\nninja.sha256=%s\n' $shuba_ninja_path $shuba_ninja_version (shuba_sha256_file $shuba_ninja_path)
        printf 'git.path=%s\ngit.version=%s\ngit.sha256=%s\n' $shuba_git_path $shuba_git_version (shuba_sha256_file $shuba_git_path)
        printf 'c_compiler.path=%s\nc_compiler.version=%s\nc_compiler.sha256=%s\n' $shuba_c_compiler $shuba_c_compiler_version (shuba_sha256_file $shuba_c_compiler)
        printf 'cxx_compiler.path=%s\ncxx_compiler.version=%s\ncxx_compiler.sha256=%s\n' $shuba_cxx_compiler $shuba_cxx_compiler_version (shuba_sha256_file $shuba_cxx_compiler)
        printf '%s\n' 'cmake_definition=CMAKE_BUILD_TYPE=Release' 'cmake_definition=JUCE_BUILD_EXAMPLES=OFF' 'cmake_definition=JUCE_BUILD_EXTRAS=ON'
        cat $shuba_tool_descriptor
        cat $shuba_source_manifest
    end >$shuba_output
    set --local shuba_descriptor_status $status
    rm -f -- $shuba_tool_descriptor $shuba_source_manifest
    return $shuba_descriptor_status
end

function shuba_projucer_main
    set --local shuba_mode build
    set --local shuba_lock_held false
    if test (count $argv) -eq 2; and test "$argv[1]" = --lock-held
        set shuba_lock_held true
        set shuba_mode $argv[2]
        if not contains -- $shuba_mode build clean check print-path print-fingerprint
            shuba_fail 'invalid internal Projucer mode'
            return 1
        end
    else if test (count $argv) -gt 1
        shuba_fail 'only one Projucer option may be supplied'
        return 1
    else if test (count $argv) -eq 1
        switch $argv[1]
            case --help
                shuba_projucer_usage
                return 0
            case --clean --check --print-path --print-fingerprint
                set shuba_mode (string replace -- '--' '' $argv[1])
            case '*'
                shuba_projucer_usage >&2
                shuba_fail "unknown Projucer option: $argv[1]"
                return 1
        end
    end
    set --local shuba_tools_root (realpath --canonicalize-existing -- (status dirname)); or return 1
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_tools_root/../..); or return 1
    set --local shuba_source_root $shuba_project_root/third_party/JUCE
    set --local shuba_build_parent $shuba_project_root/build
    set --local shuba_build_root $shuba_build_parent/projucer-release
    set --local shuba_executable $shuba_build_root/extras/Projucer/Projucer_artefacts/Release/Projucer
    set --local shuba_stamp $shuba_build_root/.shuba-projucer-build.stamp
    set --local shuba_lock_file $shuba_build_parent/.shuba-projucer-release.lock
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_require_fish_version (shuba_contract_get tool.fish_min_version); or return 1
    shuba_validate_owned_directory $shuba_build_parent $shuba_project_root 'owned build parent'; or return 1
    if test $shuba_lock_held != true
        set --local shuba_flock_path (shuba_resolve_command flock); or return 1
        set --local shuba_current_fish (realpath --canonicalize-existing -- (status fish-path)); or return 1
        $shuba_flock_path --exclusive $shuba_lock_file $shuba_current_fish --no-config \
            $shuba_tools_root/build-projucer.fish --lock-held $shuba_mode
        return $status
    end
    shuba_validate_structured_tools; or return 1
    shuba_validate_android_toolchain; or return 1
    if test -L $shuba_build_root
        shuba_fail 'owned Projucer build root must not be a symbolic link'
        return 1
    end
    set --global shuba_git_path (shuba_resolve_command git); or return 1
    shuba_projucer_validate_source $shuba_project_root $shuba_source_root; or return 1
    set --global shuba_cmake_path (shuba_resolve_command cmake); or return 1
    set --global shuba_ninja_path (shuba_resolve_command ninja); or return 1
    set --global shuba_c_compiler (shuba_resolve_command (set --query CC; and echo $CC; or echo cc)); or return 1
    set --global shuba_cxx_compiler (shuba_resolve_command (set --query CXX; and echo $CXX; or echo c++)); or return 1
    set --global shuba_cmake_version (shuba_first_line $shuba_cmake_path --version); or return 1
    set --global shuba_ninja_version (shuba_first_line $shuba_ninja_path --version); or return 1
    set --global shuba_c_compiler_version (shuba_first_line $shuba_c_compiler --version); or return 1
    set --global shuba_cxx_compiler_version (shuba_first_line $shuba_cxx_compiler --version); or return 1
    set --global shuba_git_version (shuba_first_line $shuba_git_path --version); or return 1
    set --global shuba_projucer_descriptor (mktemp $shuba_build_parent/.shuba-projucer-fingerprint.XXXXXX); or return 1
    set --local shuba_descriptor $shuba_projucer_descriptor
    shuba_projucer_write_descriptor $shuba_project_root $shuba_source_root $shuba_descriptor; or return 1
    set --local shuba_fingerprint (shuba_sha256_file $shuba_descriptor); or return 1
    if test $shuba_mode = print-fingerprint
        rm -f -- $shuba_descriptor
        printf '%s\n' $shuba_fingerprint
        return 0
    end
    set --local shuba_cache_valid false
    if shuba_stamp_matches $shuba_stamp 2 $shuba_fingerprint; and shuba_projucer_validate_outputs $shuba_source_root $shuba_build_root $shuba_executable
        set shuba_cache_valid true
    end
    if test $shuba_mode = check; or test $shuba_mode = print-path
        rm -f -- $shuba_descriptor
        if test $shuba_cache_valid != true
            shuba_fail "cached Projucer is absent, stale, or invalid for fingerprint $shuba_fingerprint"
            return 1
        end
        test $shuba_mode = print-path; and printf '%s\n' $shuba_executable
        return 0
    end
    if test $shuba_mode = clean; or test $shuba_cache_valid != true
        rm -rf -- $shuba_build_root
    else
        rm -f -- $shuba_descriptor
        printf 'Projucer build: reusing validated fingerprint %s\n' $shuba_fingerprint
        return 0
    end
    mkdir -p -- $shuba_build_root; or return 1
    $shuba_cmake_path -S $shuba_source_root -B $shuba_build_root -G Ninja \
        -DCMAKE_MAKE_PROGRAM=$shuba_ninja_path -DCMAKE_C_COMPILER=$shuba_c_compiler -DCMAKE_CXX_COMPILER=$shuba_cxx_compiler \
        -DCMAKE_BUILD_TYPE=Release -DJUCE_BUILD_EXAMPLES=OFF -DJUCE_BUILD_EXTRAS=ON; or return 1
    $shuba_cmake_path --build $shuba_build_root --config Release --target Projucer --parallel; or return 1
    shuba_projucer_validate_outputs $shuba_source_root $shuba_build_root $shuba_executable; or return 1
    shuba_write_stamp $shuba_stamp 2 $shuba_fingerprint $shuba_descriptor; or return 1
    rm -f -- $shuba_descriptor
    printf 'Projucer build: created validated fingerprint %s\n' $shuba_fingerprint
end

set --global shuba_projucer_descriptor ''
function shuba_projucer_cleanup
    if test -n "$shuba_projucer_descriptor"
        rm -f -- $shuba_projucer_descriptor $shuba_projucer_descriptor.tools $shuba_projucer_descriptor.sources
    end
end
function shuba_projucer_exit_cleanup --on-event fish_exit
    shuba_projucer_cleanup
end
function shuba_projucer_interrupt_cleanup --on-signal INT
    shuba_projucer_cleanup
    exit 130
end
function shuba_projucer_terminate_cleanup --on-signal TERM
    shuba_projucer_cleanup
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

shuba_projucer_main $argv
set --local shuba_main_status $status
shuba_projucer_cleanup
exit $shuba_main_status
