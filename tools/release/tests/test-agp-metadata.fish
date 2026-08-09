#!/usr/bin/fish --no-config

function shuba_agp_test_fail
    printf 'R12F AGP metadata tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_agp_write_metadata --argument-names shuba_case_root shuba_relative_directory shuba_application_id shuba_version_code shuba_version_name shuba_output_file shuba_variant_name
    set --local shuba_output_directory $shuba_case_root/$shuba_relative_directory
    mkdir -p -- $shuba_output_directory; or return 1
    $shuba_jq_path --null-input \
        --arg application_id $shuba_application_id \
        --argjson version_code $shuba_version_code \
        --arg version_name $shuba_version_name \
        --arg output_file $shuba_output_file \
        --arg variant_name $shuba_variant_name \
        '{
          version: 3,
          artifactType: {type: "APK", kind: "Directory"},
          applicationId: $application_id,
          variantName: $variant_name,
          elements: [{
            type: "SINGLE",
            filters: [],
            attributes: [],
            versionCode: $version_code,
            versionName: $version_name,
            outputFile: $output_file
          }],
          elementType: "File",
          minSdkVersionForDexing: 34
        }' >$shuba_output_directory/output-metadata.json
    or return 1
    printf 'fake APK payload\n' >$shuba_output_directory/$shuba_output_file
end

function shuba_agp_run_new --argument-names shuba_case_root
    set --global shuba_agp_new_output (shuba_find_android_release_apk \
        $shuba_case_root \
        (shuba_contract_get app.application_id) \
        (shuba_contract_get app.version_code) \
        (shuba_contract_get app.version_name) 2>$shuba_case_root/new.stderr)
    set --global shuba_agp_new_status $status
end

function shuba_agp_require_verdict --argument-names shuba_case_root shuba_expected_acceptance shuba_description
    shuba_agp_run_new $shuba_case_root
    set --local shuba_new_accepted false
    test $shuba_agp_new_status -eq 0; and set shuba_new_accepted true
    if test $shuba_new_accepted != $shuba_expected_acceptance
        shuba_agp_test_fail "AGP metadata implementation produced the wrong verdict for $shuba_description"
        return 1
    end
end

function shuba_agp_require_new_rejection --argument-names shuba_case_root shuba_description
    shuba_agp_run_new $shuba_case_root
    if test $shuba_agp_new_status -eq 0
        shuba_agp_test_fail "new implementation accepted $shuba_description"
        return 1
    end
end

function shuba_agp_new_case --argument-names shuba_case_name
    set --local shuba_case_root $shuba_agp_test_root/$shuba_case_name
    mkdir -p -- $shuba_case_root; or return 1
    printf '%s\n' $shuba_case_root
end

function shuba_agp_test_main
    set --local shuba_script_directory (status dirname)
    set --local shuba_project_root (realpath --canonicalize-existing -- $shuba_script_directory/../../..); or return 1
    set --global shuba_release_tools_root $shuba_project_root/tools/release
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_validate_structured_tools; or return 1
    set --global shuba_agp_test_root (mktemp --directory /tmp/shuba-r12f-agp-metadata.XXXXXX); or return 1

    set --local shuba_case_root (shuba_agp_new_case positive); or return 1
    shuba_agp_write_metadata $shuba_case_root release_/release com.shuba.catalog 1 1.0.0 app-release_-release.apk release_Release; or return 1
    shuba_agp_require_verdict $shuba_case_root true positive; or return 1

    set shuba_case_root (shuba_agp_new_case absent); or return 1
    shuba_agp_require_verdict $shuba_case_root false absent-metadata; or return 1

    set shuba_case_root (shuba_agp_new_case duplicate); or return 1
    shuba_agp_write_metadata $shuba_case_root release_/a com.shuba.catalog 1 1.0.0 a.apk release_Release; or return 1
    shuba_agp_write_metadata $shuba_case_root release_/b com.shuba.catalog 1 1.0.0 b.apk release_Release; or return 1
    shuba_agp_require_verdict $shuba_case_root false duplicate-release-output; or return 1

    set shuba_case_root (shuba_agp_new_case wrong-identity); or return 1
    shuba_agp_write_metadata $shuba_case_root release_/release com.shuba.catalog 2 1.0.0 app.apk release_Release; or return 1
    shuba_agp_require_verdict $shuba_case_root false wrong-version-code; or return 1

    set shuba_case_root (shuba_agp_new_case traversal); or return 1
    shuba_agp_write_metadata $shuba_case_root release_/release com.shuba.catalog 1 1.0.0 placeholder.apk release_Release; or return 1
    $shuba_jq_path '.elements[0].outputFile = "../outside.apk"' $shuba_case_root/release_/release/output-metadata.json >$shuba_case_root/mutated
    and mv $shuba_case_root/mutated $shuba_case_root/release_/release/output-metadata.json
    or return 1
    shuba_agp_require_verdict $shuba_case_root false traversal-filename; or return 1

    set shuba_case_root (shuba_agp_new_case malformed-json); or return 1
    mkdir -p $shuba_case_root/release
    printf '{invalid\n' >$shuba_case_root/release/output-metadata.json
    shuba_agp_require_verdict $shuba_case_root false malformed-json; or return 1

    set shuba_case_root (shuba_agp_new_case wrong-types); or return 1
    shuba_agp_write_metadata $shuba_case_root release com.shuba.catalog 1 1.0.0 app.apk release_Release; or return 1
    $shuba_jq_path '.elements[0].versionCode = true' $shuba_case_root/release/output-metadata.json >$shuba_case_root/mutated
    and mv $shuba_case_root/mutated $shuba_case_root/release/output-metadata.json
    or return 1
    shuba_agp_require_verdict $shuba_case_root false wrong-field-type; or return 1

    set shuba_case_root (shuba_agp_new_case ignored-variant); or return 1
    shuba_agp_write_metadata $shuba_case_root debug com.shuba.catalog 1 1.0.0 debug.apk debug_Debug; or return 1
    shuba_agp_write_metadata $shuba_case_root release com.shuba.catalog 1 1.0.0 release.apk release_Release; or return 1
    shuba_agp_require_verdict $shuba_case_root true ignored-nonrelease-variant; or return 1

    set shuba_case_root (shuba_agp_new_case control-character); or return 1
    shuba_agp_write_metadata $shuba_case_root release com.shuba.catalog 1 1.0.0 placeholder.apk release_Release; or return 1
    $shuba_jq_path '.elements[0].outputFile = "bad\u0001.apk"' $shuba_case_root/release/output-metadata.json >$shuba_case_root/mutated
    and mv $shuba_case_root/mutated $shuba_case_root/release/output-metadata.json
    or return 1
    shuba_agp_require_new_rejection $shuba_case_root control-character-filename; or return 1

    set shuba_case_root (shuba_agp_new_case nonascii-filename); or return 1
    shuba_agp_write_metadata $shuba_case_root release com.shuba.catalog 1 1.0.0 placeholder.apk release_Release; or return 1
    $shuba_jq_path '.elements[0].outputFile = "Шуба.apk"' $shuba_case_root/release/output-metadata.json >$shuba_case_root/mutated
    and mv $shuba_case_root/mutated $shuba_case_root/release/output-metadata.json
    or return 1
    shuba_agp_require_new_rejection $shuba_case_root non-ASCII-filename; or return 1

    set shuba_case_root (shuba_agp_new_case symlink-metadata); or return 1
    mkdir -p $shuba_case_root/release
    printf '{}\n' >$shuba_case_root/metadata-target.json
    ln -s ../../metadata-target.json $shuba_case_root/release/output-metadata.json
    shuba_agp_require_new_rejection $shuba_case_root symbolic-link-metadata; or return 1

    set shuba_case_root (shuba_agp_new_case symlink-apk); or return 1
    shuba_agp_write_metadata $shuba_case_root release com.shuba.catalog 1 1.0.0 app.apk release_Release; or return 1
    rm $shuba_case_root/release/app.apk
    printf 'outside\n' >$shuba_case_root/outside.apk
    ln -s ../outside.apk $shuba_case_root/release/app.apk
    shuba_agp_require_new_rejection $shuba_case_root symbolic-link-APK; or return 1

    printf '%s\n' 'R12F AGP metadata positive and adversarial jq/fish probes: passed'
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-contract.fish
source $shuba_script_directory/../lib/android-toolchain.fish
source $shuba_script_directory/../lib/agp-metadata.fish

set --global shuba_agp_test_root ''
function shuba_agp_test_cleanup --on-event fish_exit
    if test -n "$shuba_agp_test_root"; and test -d $shuba_agp_test_root
        rm -rf -- $shuba_agp_test_root
    end
end

shuba_agp_test_main
set --local shuba_main_status $status
exit $shuba_main_status
