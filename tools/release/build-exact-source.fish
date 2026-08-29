#!/usr/bin/fish --no-config

function shuba_build_exact_source_usage
    printf '%s\n' \
        'Usage: tools/release/build-exact-source.fish --tag v<version> --output DIRECTORY' \
        '' \
        'Build and verify a deterministic recursive corresponding-source asset from an' \
        'unsigned annotated release tag and its exact initialized submodule commits.'
end

function shuba_build_exact_source_main
    if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_build_exact_source_usage
        return 0
    end
    if test (count $argv) -ne 4; or test $argv[1] != --tag; or test $argv[3] != --output
        shuba_build_exact_source_usage >&2
        shuba_fail 'exact-source builder requires --tag and --output in that order'
        return 1
    end
    set --local shuba_project_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    shuba_exact_source_build $shuba_project_root $argv[2] $argv[4]
end

umask 022
set --global --export LC_ALL C
set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/atomic-publication.fish
source $shuba_script_directory/lib/exact-source.fish

shuba_build_exact_source_main $argv
exit $status
