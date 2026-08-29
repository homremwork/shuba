#!/usr/bin/fish --no-config

function shuba_verify_publication_bundle_usage
    printf '%s\n' \
        'Usage: tools/release/verify-publication-bundle.fish --tag v<version> --bundle-dir DIRECTORY' \
        '' \
        'Verify public-bundle inventory, APK packet identity, exact corresponding source,' \
        'tag/provenance alignment, standalone notices, release notes, and checksums.'
end

function shuba_verify_publication_bundle_main
    if test (count $argv) -eq 1; and test $argv[1] = --help
        shuba_verify_publication_bundle_usage
        return 0
    end
    if test (count $argv) -ne 4; or test $argv[1] != --tag; or test $argv[3] != --bundle-dir
        shuba_verify_publication_bundle_usage >&2
        shuba_fail 'publication-bundle verifier requires --tag and --bundle-dir in that order'
        return 1
    end
    set --local shuba_project_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    shuba_publication_bundle_verify $shuba_project_root $argv[2] $argv[4]
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

shuba_verify_publication_bundle_main $argv
exit $status
