function shuba_publication_bundle_fail
    shuba_fail 'publication bundle:' $argv
end

function shuba_publication_bundle_stem
    set --local shuba_app_name (shuba_contract_get app.name); or return 1
    set shuba_app_name (string replace --all ' ' - -- $shuba_app_name)
    set --local shuba_version (shuba_contract_get app.version_name); or return 1
    printf '%s-%s\n' $shuba_app_name $shuba_version
end

function shuba_publication_bundle_expected_names --argument-names shuba_stem shuba_apk_name
    printf '%s\n' \
        $shuba_apk_name \
        $shuba_apk_name.sha256 \
        $shuba_apk_name.provenance.txt \
        $shuba_apk_name.verification.txt \
        $shuba_stem-source.tar.zst \
        $shuba_stem-source.tar.zst.sha256 \
        $shuba_stem-source.inventory.txt \
        $shuba_stem-source.submodules.txt \
        LICENSE \
        THIRD_PARTY_NOTICES.md \
        $shuba_stem-release-notes.md \
        $shuba_stem-SHA256SUMS | sort
end

function shuba_publication_bundle_validate_component --argument-names shuba_component
    if test -z "$shuba_component"; or test "$shuba_component" = .; or test "$shuba_component" = ..
        shuba_publication_bundle_fail 'output path has an empty, dot, or traversal component'
        return 1
    end
    if string match --regex --quiet '[[:cntrl:]|/]' -- $shuba_component
        shuba_publication_bundle_fail 'output path has a control character, inventory separator, or slash in a component'
        return 1
    end
    return 0
end

function shuba_publication_bundle_output_destination --argument-names shuba_requested_path
    set --local shuba_parent (realpath --canonicalize-existing -- (path dirname -- $shuba_requested_path)); or begin
        shuba_publication_bundle_fail "bundle output parent is unavailable: $shuba_requested_path"
        return 1
    end
    set --local shuba_name (path basename -- $shuba_requested_path)
    shuba_publication_bundle_validate_component $shuba_name; or return 1
    printf '%s\n' $shuba_parent/$shuba_name
end

function shuba_publication_bundle_verify_safe_text --argument-names shuba_path shuba_label
    shuba_require_regular_file $shuba_path; or return 1
    if test (stat --format %a -- $shuba_path) != 644
        shuba_publication_bundle_fail "$shuba_label mode is not public-safe"
        return 1
    end
    if test (stat --format %s -- $shuba_path) -le 0
        shuba_publication_bundle_fail "$shuba_label must not be empty"
        return 1
    end
    grep --binary-files=without-match --quiet --extended-regexp \
        -- '-----BEGIN( [A-Za-z0-9]+)* PRIVATE KEY-----|SHUBA_ANDROID_(STORE|KEY)_PASSWORD|SHUBA_ANDROID_KEYSTORE_BASE64|/home/[^[:space:]/]+' $shuba_path
    set --local shuba_forbidden_status $status
    if test $shuba_forbidden_status -eq 0
        shuba_publication_bundle_fail "$shuba_label contains private material or a personal absolute path"
        return 1
    else if test $shuba_forbidden_status -gt 1
        shuba_publication_bundle_fail "could not inspect $shuba_label for private material"
        return 1
    end
    return 0
end

function shuba_publication_bundle_provenance_get --argument-names shuba_provenance_path shuba_key
    awk -v expected_key=$shuba_key '
        $0 == "release_state_begin" { exit }
        index($0, expected_key "=") == 1 {
            count += 1
            value = substr($0, length(expected_key) + 2)
        }
        END {
            if (count == 1 && length(value) > 0) {
                print value
                exit 0
            }
            exit 1
        }
    ' $shuba_provenance_path
end

function shuba_publication_bundle_release_state_get --argument-names shuba_provenance_path shuba_key
    awk -v expected_key=$shuba_key '
        $0 == "release_state_begin" { inside = 1; next }
        $0 == "release_state_end" { exit }
        inside && index($0, expected_key "=") == 1 {
            count += 1
            value = substr($0, length(expected_key) + 2)
        }
        END {
            if (count == 1 && length(value) > 0) {
                print value
                exit 0
            }
            exit 1
        }
    ' $shuba_provenance_path
end

function shuba_publication_bundle_validate_packet --argument-names shuba_project_root shuba_packet_directory
    set --global shuba_release_project_root $shuba_project_root
    set --local shuba_verifier $shuba_project_root/tools/release/verify-android-apk.fish
    shuba_require_executable_file $shuba_verifier; or return 1
    set --local shuba_apk_name (shuba_contract_get artifact.basename); or return 1
    set --local shuba_packet_copy (mktemp --directory /tmp/shuba-publication-packet.XXXXXX); or return 1
    for shuba_name in \
        $shuba_apk_name $shuba_apk_name.sha256 $shuba_apk_name.provenance.txt $shuba_apk_name.verification.txt
        cp -- $shuba_packet_directory/$shuba_name $shuba_packet_copy/$shuba_name; or begin
            rm -rf -- $shuba_packet_copy
            return 1
        end
    end
    shuba_validate_artifact_directory $shuba_packet_copy $shuba_apk_name $shuba_verifier
    set --local shuba_packet_status $status
    rm -rf -- $shuba_packet_copy
    if test $shuba_packet_status -ne 0
        return $shuba_packet_status
    end
    set --local shuba_provenance $shuba_packet_directory/$shuba_apk_name.provenance.txt
    shuba_validate_release_provenance $shuba_provenance; or return 1
    for shuba_key in artifact.basename app.version_name app.version_code signing.certificate_sha256
        set --local shuba_expected (shuba_contract_get $shuba_key); or return 1
        set --local shuba_actual (shuba_publication_bundle_provenance_get $shuba_provenance $shuba_key); or begin
            shuba_publication_bundle_fail "APK provenance lacks a unique public record: $shuba_key"
            return 1
        end
        if test "$shuba_actual" != "$shuba_expected"
            shuba_publication_bundle_fail "APK provenance differs from the release contract: $shuba_key"
            return 1
        end
    end
    set --local shuba_root_commit (shuba_publication_bundle_release_state_get $shuba_provenance root_commit); or begin
        shuba_publication_bundle_fail 'APK provenance lacks a unique release-state root commit'
        return 1
    end
    if not string match --regex --quiet '^[0-9a-f]{40,64}$' -- $shuba_root_commit
        shuba_publication_bundle_fail 'APK provenance root commit is malformed'
        return 1
    end
    set --global shuba_publication_bundle_packet_root_commit $shuba_root_commit
end

function shuba_publication_bundle_verify_manifest --argument-names shuba_manifest_path shuba_bundle_directory
    set --local shuba_expected_names $argv[3..]
    if test (count $shuba_expected_names) -eq 0
        shuba_publication_bundle_fail 'overall SHA-256 manifest has no expected public assets'
        return 1
    end
    shuba_require_regular_file $shuba_manifest_path; or return 1
    if test (stat --format %a -- $shuba_manifest_path) != 644
        shuba_publication_bundle_fail 'overall SHA-256 manifest mode is not public-safe'
        return 1
    end
    set --local shuba_expected_manifest (mktemp /tmp/shuba-publication-manifest-expected.XXXXXX); or return 1
    for shuba_name in $shuba_expected_names
        if string match --quiet '*-SHA256SUMS' -- $shuba_name
            continue
        end
        printf '%s  %s\n' (shuba_sha256_file $shuba_bundle_directory/$shuba_name) $shuba_name >>$shuba_expected_manifest; or begin
            rm -f -- $shuba_expected_manifest
            return 1
        end
    end
    sort -- $shuba_expected_manifest >$shuba_expected_manifest.sorted; or begin
        rm -f -- $shuba_expected_manifest $shuba_expected_manifest.sorted
        return 1
    end
    mv --force -- $shuba_expected_manifest.sorted $shuba_expected_manifest; or begin
        rm -f -- $shuba_expected_manifest
        return 1
    end
    cmp --silent $shuba_expected_manifest $shuba_manifest_path
    set --local shuba_manifest_status $status
    rm -f -- $shuba_expected_manifest
    if test $shuba_manifest_status -ne 0
        shuba_publication_bundle_fail 'overall SHA-256 manifest does not cover each and only each other public asset'
        return 1
    end
end

function shuba_publication_bundle_validate_directory --argument-names shuba_directory shuba_stem shuba_apk_name
    if not test -d $shuba_directory; or test -L $shuba_directory
        shuba_publication_bundle_fail "bundle directory is unavailable or symbolic-linked: $shuba_directory"
        return 1
    end
    set --local shuba_expected_names (shuba_publication_bundle_expected_names $shuba_stem $shuba_apk_name)
    set --local shuba_expected (mktemp /tmp/shuba-publication-expected.XXXXXX); or return 1
    set --local shuba_actual (mktemp /tmp/shuba-publication-actual.XXXXXX); or begin
        rm -f -- $shuba_expected
        return 1
    end
    printf '%s\n' $shuba_expected_names >$shuba_expected
    find -P $shuba_directory -mindepth 1 -maxdepth 1 -printf '%f\n' | sort >$shuba_actual
    set --local shuba_inventory_statuses $pipestatus
    for shuba_status in $shuba_inventory_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_expected $shuba_actual
            return 1
        end
    end
    cmp --silent $shuba_expected $shuba_actual
    set --local shuba_inventory_status $status
    rm -f -- $shuba_expected $shuba_actual
    if test $shuba_inventory_status -ne 0
        shuba_publication_bundle_fail 'bundle directory has an unexpected public-asset inventory'
        return 1
    end
    for shuba_name in $shuba_expected_names
        shuba_require_regular_file $shuba_directory/$shuba_name; or return 1
        if test (stat --format %a -- $shuba_directory/$shuba_name) != 644
            shuba_publication_bundle_fail "bundle asset mode is not public-safe: $shuba_name"
            return 1
        end
    end
    set --global shuba_publication_bundle_expected_names $shuba_expected_names
end

function shuba_publication_bundle_verify --argument-names shuba_project_root shuba_tag shuba_bundle_directory
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_exact_source_resolve_tag $shuba_project_root $shuba_tag; or return 1
    set --local shuba_stem (shuba_publication_bundle_stem); or return 1
    set --local shuba_apk_name (shuba_contract_get artifact.basename); or return 1
    shuba_publication_bundle_validate_directory $shuba_bundle_directory $shuba_stem $shuba_apk_name; or return 1
    shuba_publication_bundle_validate_packet $shuba_project_root $shuba_bundle_directory; or return 1
    if test "$shuba_publication_bundle_packet_root_commit" != "$shuba_exact_source_commit"
        shuba_publication_bundle_fail 'APK provenance root commit differs from the annotated release tag target'
        return 1
    end
    shuba_exact_source_verify $shuba_project_root $shuba_tag $shuba_bundle_directory true; or return 1
    for shuba_name in LICENSE THIRD_PARTY_NOTICES.md $shuba_stem-release-notes.md
        shuba_publication_bundle_verify_safe_text $shuba_bundle_directory/$shuba_name $shuba_name; or return 1
    end
    cmp --silent $shuba_project_root/LICENSE $shuba_bundle_directory/LICENSE; or begin
        shuba_publication_bundle_fail 'standalone project license differs from the tagged source'
        return 1
    end
    cmp --silent $shuba_project_root/THIRD_PARTY_NOTICES.md $shuba_bundle_directory/THIRD_PARTY_NOTICES.md; or begin
        shuba_publication_bundle_fail 'standalone third-party notices differ from the tagged source'
        return 1
    end
    shuba_publication_bundle_verify_manifest $shuba_bundle_directory/$shuba_stem-SHA256SUMS \
        $shuba_bundle_directory $shuba_publication_bundle_expected_names
end

function shuba_publication_bundle_write_manifest --argument-names shuba_manifest_path shuba_bundle_directory
    set --local shuba_expected_names $argv[3..]
    if test (count $shuba_expected_names) -eq 0
        shuba_publication_bundle_fail 'overall SHA-256 manifest has no expected public assets'
        return 1
    end
    for shuba_name in $shuba_expected_names
        if string match --quiet '*-SHA256SUMS' -- $shuba_name
            continue
        end
        printf '%s  %s\n' (shuba_sha256_file $shuba_bundle_directory/$shuba_name) $shuba_name; or return 1
    end | sort >$shuba_manifest_path
    chmod 0644 -- $shuba_manifest_path
end

function shuba_publication_bundle_build --argument-names shuba_project_root shuba_tag shuba_packet_directory shuba_source_directory shuba_release_notes shuba_requested_output
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_exact_source_resolve_tag $shuba_project_root $shuba_tag; or return 1
    set --local shuba_stem (shuba_publication_bundle_stem); or return 1
    set --local shuba_apk_name (shuba_contract_get artifact.basename); or return 1
    shuba_publication_bundle_validate_packet $shuba_project_root $shuba_packet_directory; or return 1
    if test "$shuba_publication_bundle_packet_root_commit" != "$shuba_exact_source_commit"
        shuba_publication_bundle_fail 'APK provenance root commit differs from the annotated release tag target'
        return 1
    end
    shuba_exact_source_verify $shuba_project_root $shuba_tag $shuba_source_directory false; or return 1
    shuba_publication_bundle_verify_safe_text $shuba_release_notes 'operator-provided release notes'; or return 1
    set --local shuba_output (shuba_publication_bundle_output_destination $shuba_requested_output); or return 1
    set --local shuba_expected_names (shuba_publication_bundle_expected_names $shuba_stem $shuba_apk_name)
    set --local shuba_stage_parent (path dirname -- $shuba_output)
    shuba_atomic_publication_initialize $shuba_stage_parent $shuba_output 'publication bundle'; or return 1
    set --local shuba_stage (shuba_atomic_publication_stage_path); or return 1
    for shuba_name in \
        $shuba_apk_name $shuba_apk_name.sha256 $shuba_apk_name.provenance.txt $shuba_apk_name.verification.txt
        cp -- $shuba_packet_directory/$shuba_name $shuba_stage/$shuba_name; or begin
            shuba_atomic_publication_cleanup 1
            return 1
        end
    end
    for shuba_name in \
        $shuba_stem-source.tar.zst $shuba_stem-source.tar.zst.sha256 \
        $shuba_stem-source.inventory.txt $shuba_stem-source.submodules.txt
        cp -- $shuba_source_directory/$shuba_name $shuba_stage/$shuba_name; or begin
            shuba_atomic_publication_cleanup 1
            return 1
        end
    end
    cp -- $shuba_project_root/LICENSE $shuba_stage/LICENSE; or begin
        shuba_atomic_publication_cleanup 1
        return 1
    end
    cp -- $shuba_project_root/THIRD_PARTY_NOTICES.md $shuba_stage/THIRD_PARTY_NOTICES.md; or begin
        shuba_atomic_publication_cleanup 1
        return 1
    end
    cp -- $shuba_release_notes $shuba_stage/$shuba_stem-release-notes.md; or begin
        shuba_atomic_publication_cleanup 1
        return 1
    end
    for shuba_name in $shuba_expected_names
        if string match --quiet '*-SHA256SUMS' -- $shuba_name
            continue
        end
        chmod 0644 -- $shuba_stage/$shuba_name; or begin
            shuba_atomic_publication_cleanup 1
            return 1
        end
    end
    shuba_publication_bundle_write_manifest $shuba_stage/$shuba_stem-SHA256SUMS $shuba_stage $shuba_expected_names; or begin
        shuba_atomic_publication_cleanup 1
        return 1
    end
    shuba_atomic_publication_publish; or begin
        shuba_atomic_publication_cleanup 1
        return 1
    end
    shuba_publication_bundle_verify $shuba_project_root $shuba_tag $shuba_output
    set --local shuba_verify_status $status
    if test $shuba_verify_status -ne 0
        shuba_atomic_publication_cleanup $shuba_verify_status
        return $shuba_verify_status
    end
    shuba_atomic_publication_commit; or begin
        shuba_atomic_publication_cleanup 1
        return 1
    end
end
