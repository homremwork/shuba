#!/usr/bin/fish --no-config

function shuba_provenance_test_fail
    printf 'R12F release provenance tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_provenance_test_expect_rejection --argument-names shuba_description shuba_fixture shuba_expected_message
    set --local shuba_output (shuba_validate_release_provenance $shuba_fixture 2>&1)
    set --local shuba_status $status
    if test $shuba_status -eq 0
        shuba_provenance_test_fail "accepted $shuba_description"
        return 1
    end
    if not string match --quiet "*$shuba_expected_message*" -- (string join \n -- $shuba_output)
        shuba_provenance_test_fail "reported the wrong failure for $shuba_description"
        return 1
    end
end

function shuba_provenance_test_write_valid --argument-names shuba_path
    begin
        for shuba_key in \
            provenance_schema_version artifact.basename artifact.sha256 artifact.bytes \
            release_contract.sha256 release_entrypoint.sha256 signing.certificate_sha256 \
            tool.java.home tool.java.version tool.java.sha256 tool.javac.version tool.javac.sha256 \
            tool.git.path tool.git.version tool.git.sha256 \
            tool.fish.path tool.fish.version tool.fish.sha256 \
            tool.jq.path tool.jq.version tool.jq.sha256 \
            tool.xmlstarlet.path tool.xmlstarlet.resolved_path tool.xmlstarlet.sha256 \
            tool.xmlstarlet.version_output_sha256 tool.xmlstarlet.linked_library_versions_sha256 \
            tool.xmlstarlet.xpath_capability tool.bsdtar.path tool.bsdtar.version tool.bsdtar.sha256 \
            tool.apkanalyzer.path tool.apkanalyzer.version tool.apkanalyzer.sha256 \
            tool.aapt2.path tool.aapt2.sha256 tool.apksigner.path tool.apksigner.sha256 \
            tool.zipalign.path tool.zipalign.sha256 tool.llvm_readelf.path tool.llvm_readelf.sha256 \
            libjxl.fingerprint_sha256 projucer.fingerprint_sha256 verification.pre_publish_sha256
            printf '%s=value\n' $shuba_key
        end
        printf '%s\n' release_state_begin release_state_end submodule_state_begin \
            submodule_state_end build_input_manifest_begin build_input_manifest_end
    end >$shuba_path
    chmod 0600 $shuba_path
end

function shuba_provenance_test_main
    set --local shuba_script_directory (status dirname)
    set --local shuba_tools_root (realpath --canonicalize-existing -- $shuba_script_directory/..); or return 1
    set --global shuba_provenance_test_root (mktemp --directory /tmp/shuba-r12f-provenance-tests.XXXXXX); or return 1
    set --local shuba_valid $shuba_provenance_test_root/valid.provenance
    shuba_provenance_test_write_valid $shuba_valid; or return 1
    shuba_validate_release_provenance $shuba_valid; or return 1

    set --local shuba_fixture $shuba_provenance_test_root/empty.provenance
    cp -- $shuba_valid $shuba_fixture
    printf 'tool.aapt2.sha256=\n' >>$shuba_fixture
    shuba_provenance_test_expect_rejection empty-value $shuba_fixture \
        'empty machine-readable value'; or return 1

    set shuba_fixture $shuba_provenance_test_root/malformed-tool.provenance
    cp -- $shuba_valid $shuba_fixture
    printf 'tool..sha256=value\n' >>$shuba_fixture
    shuba_provenance_test_expect_rejection malformed-tool $shuba_fixture \
        'malformed tool record'; or return 1

    set shuba_fixture $shuba_provenance_test_root/missing.provenance
    grep --invert-match '^tool.aapt2.sha256=' $shuba_valid >$shuba_fixture
    shuba_provenance_test_expect_rejection missing-tool $shuba_fixture \
        'requires exactly one non-empty record: tool.aapt2.sha256'; or return 1

    set shuba_fixture $shuba_provenance_test_root/duplicate.provenance
    cp -- $shuba_valid $shuba_fixture
    printf 'tool.aapt2.sha256=duplicate\n' >>$shuba_fixture
    shuba_provenance_test_expect_rejection duplicate-tool $shuba_fixture \
        'requires exactly one non-empty record: tool.aapt2.sha256'; or return 1

    set shuba_fixture $shuba_provenance_test_root/secret-name.provenance
    cp -- $shuba_valid $shuba_fixture
    printf 'SHUBA_ANDROID_STORE_PASSWORD=forbidden\n' >>$shuba_fixture
    shuba_provenance_test_expect_rejection secret-name $shuba_fixture \
        'contains a signing-password variable name'; or return 1

    if not grep --fixed-strings --quiet \
            'shuba_validate_release_provenance $shuba_output' $shuba_tools_root/lib/release-artifact.fish
        shuba_provenance_test_fail 'release writer does not invoke the provenance validator'
        return 1
    end
    printf '%s\n' 'R12F release provenance completeness and secret-boundary probes: passed'
end

set --global shuba_provenance_test_root ''
function shuba_provenance_test_cleanup --on-event fish_exit
    if test -n "$shuba_provenance_test_root"; and test -d $shuba_provenance_test_root
        rm -rf -- $shuba_provenance_test_root
    end
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-artifact.fish

shuba_provenance_test_main
set --local shuba_main_status $status
exit $shuba_main_status
