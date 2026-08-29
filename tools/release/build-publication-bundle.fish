#!/usr/bin/fish --no-config

function shuba_build_publication_bundle_usage
    printf '%s\n' \
        'Usage: tools/release/build-publication-bundle.fish --tag v<version> --packet-dir DIRECTORY --source-dir DIRECTORY --release-notes FILE --output DIRECTORY' \
        '' \
        'Combine an accepted immutable Android packet, verified exact recursive source,' \
        'standalone notices, and reviewed non-secret release notes into a public bundle.'
end

function shuba_build_publication_bundle_main
    if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_build_publication_bundle_usage
        return 0
    end
    if test (count $argv) -ne 10; or test $argv[1] != --tag; or test $argv[3] != --packet-dir; or test $argv[5] != --source-dir; or test $argv[7] != --release-notes; or test $argv[9] != --output
        shuba_build_publication_bundle_usage >&2
        shuba_fail 'publication-bundle builder requires all named arguments in the documented order'
        return 1
    end
    set --local shuba_project_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    shuba_publication_bundle_build $shuba_project_root $argv[2] $argv[4] $argv[6] $argv[8] $argv[10]
end

umask 022
set --global --export LC_ALL C
set --local shuba_script_directory (status dirname)
source $shuba_script_directory/lib/core.fish
source $shuba_script_directory/lib/release-contract.fish
source $shuba_script_directory/lib/atomic-publication.fish
source $shuba_script_directory/lib/release-artifact.fish
source $shuba_script_directory/lib/exact-source.fish
source $shuba_script_directory/lib/publication-bundle.fish

shuba_build_publication_bundle_main $argv
exit $status
