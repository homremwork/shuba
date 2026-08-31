#!/usr/bin/fish --no-config

function shuba_publication_assets_test_fail
    printf 'R12F publication-asset tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_publication_assets_expect_rejection --argument-names shuba_description shuba_expected_fragment
    set --erase argv[1..2]
    set --local shuba_output ($argv 2>&1)
    set --local shuba_status $status
    if test $shuba_status -eq 0
        shuba_publication_assets_test_fail "accepted $shuba_description"
        return 1
    end
    if not string match --quiet "*$shuba_expected_fragment*" -- (string join \n -- $shuba_output)
        shuba_publication_assets_test_fail "reported the wrong rejection for $shuba_description"
        return 1
    end
end

function shuba_publication_assets_write_project_contract --argument-names shuba_project
    mkdir -p -- $shuba_project/release; or return 1
    cp -- $shuba_publication_assets_workspace/release/release.properties $shuba_project/release/release.properties; or return 1
end

function shuba_publication_assets_initialize_repository --argument-names shuba_repository
    git -C $shuba_repository init --quiet; or return 1
    git -C $shuba_repository config user.name 'Shuba Publication Test'; or return 1
    git -C $shuba_repository config user.email 'shuba-publication-test@example.invalid'; or return 1
end

function shuba_publication_assets_commit --argument-names shuba_repository shuba_message
    git -C $shuba_repository add --all; or return 1
    git -C $shuba_repository commit --quiet --message $shuba_message
end

function shuba_publication_assets_make_project
    set --local shuba_project $shuba_publication_assets_root/project-(random)
    set --local shuba_child $shuba_publication_assets_root/child-(random)
    set --local shuba_grandchild $shuba_publication_assets_root/grandchild-(random)
    mkdir -p -- $shuba_project $shuba_child $shuba_grandchild; or return 1
    shuba_publication_assets_initialize_repository $shuba_grandchild; or return 1
    printf '%s\n' 'nested dependency fixture' >$shuba_grandchild/nested.txt; or return 1
    shuba_publication_assets_commit $shuba_grandchild nested; or return 1

    shuba_publication_assets_initialize_repository $shuba_child; or return 1
    printf '%s\n' 'dependency fixture' >$shuba_child/dependency.txt; or return 1
    shuba_publication_assets_commit $shuba_child base; or return 1
    git -C $shuba_child -c protocol.file.allow=always submodule add --quiet $shuba_grandchild vendor/nested; or return 1
    shuba_publication_assets_commit $shuba_child nested-submodule; or return 1

    shuba_publication_assets_initialize_repository $shuba_project; or return 1
    shuba_publication_assets_write_project_contract $shuba_project; or return 1
    mkdir -p -- $shuba_project/tools/release; or return 1
    printf '%s\n' '#!/bin/sh' 'exit 0' >$shuba_project/tools/release/verify-android-apk.fish; or return 1
    chmod 0755 -- $shuba_project/tools/release/verify-android-apk.fish; or return 1
    printf '%s\n' 'License fixture.' >$shuba_project/LICENSE; or return 1
    printf '%s\n' 'Third-party notices fixture.' >$shuba_project/THIRD_PARTY_NOTICES.md; or return 1
    printf '%s\n' 'project source fixture' >$shuba_project/source.txt; or return 1
    ln -s source.txt $shuba_project/source-link.txt; or return 1
    shuba_publication_assets_commit $shuba_project root; or return 1
    git -C $shuba_project -c protocol.file.allow=always submodule add --quiet $shuba_child third_party/dependency; or return 1
    git -C $shuba_project -c protocol.file.allow=always submodule update --init --recursive >/dev/null 2>&1; or return 1
    shuba_publication_assets_commit $shuba_project recursive-submodules; or return 1
    env GIT_COMMITTER_DATE='2026-08-28T00:00:00Z' GIT_AUTHOR_DATE='2026-08-28T00:00:00Z' \
        git -C $shuba_project tag --annotate v1.0.2 --message 'Shuba 1.0.2 publication fixture'; or return 1
    printf '%s\n' $shuba_project
end

function shuba_publication_assets_test_tag_notes --argument-names shuba_project
    set --local shuba_outputs $shuba_publication_assets_root/tag-notes
    mkdir -p -- $shuba_outputs; or return 1
    printf '%s\n' 'Shuba 1.0.2 publication fixture' >$shuba_outputs/expected.md; or return 1
    shuba_exact_source_write_tag_notes $shuba_project v1.0.2 $shuba_outputs/release-notes.md; or return 1
    cmp --silent $shuba_outputs/expected.md $shuba_outputs/release-notes.md; or begin
        shuba_publication_assets_test_fail 'annotated tag notes were not extracted exactly'
        return 1
    end
    if test (stat --format %a -- $shuba_outputs/release-notes.md) != 644
        shuba_publication_assets_test_fail 'annotated tag notes mode is not public-safe'
        return 1
    end

    ln -s release-notes.md $shuba_outputs/linked-notes.md; or return 1
    shuba_publication_assets_expect_rejection linked-notes-output \
        'must not be a directory or symbolic link' \
        shuba_exact_source_write_tag_notes $shuba_project v1.0.2 $shuba_outputs/linked-notes.md; or return 1

    git -C $shuba_project tag --delete v1.0.2 >/dev/null; or return 1
    git -C $shuba_project tag v1.0.2; or return 1
    shuba_publication_assets_expect_rejection lightweight-tag 'must exist and be annotated' \
        shuba_exact_source_write_tag_notes $shuba_project v1.0.2 $shuba_outputs/lightweight.md; or return 1

    git -C $shuba_project tag --delete v1.0.2 >/dev/null; or return 1
    printf '%s\n' '   ' >$shuba_outputs/blank-message; or return 1
    env GIT_COMMITTER_DATE='2026-08-28T00:00:00Z' GIT_AUTHOR_DATE='2026-08-28T00:00:00Z' \
        git -C $shuba_project tag --annotate v1.0.2 --cleanup=verbatim \
        --file $shuba_outputs/blank-message; or return 1
    shuba_publication_assets_expect_rejection blank-tag-notes 'must contain non-empty release notes' \
        shuba_exact_source_write_tag_notes $shuba_project v1.0.2 $shuba_outputs/blank.md; or return 1

    git -C $shuba_project tag --delete v1.0.2 >/dev/null; or return 1
    env GIT_COMMITTER_DATE='2026-08-28T00:00:00Z' GIT_AUTHOR_DATE='2026-08-28T00:00:00Z' \
        git -C $shuba_project tag --annotate v1.0.2 \
        --message 'Shuba 1.0.2 publication fixture'; or return 1
end

function shuba_publication_assets_write_packet_provenance --argument-names shuba_path shuba_basename shuba_digest shuba_bytes shuba_root_commit
    begin
        printf '%s\n' provenance_schema_version=value
        for shuba_key in \
            release_contract.sha256 release_entrypoint.sha256 \
            tool.java.home tool.java.version tool.java.sha256 tool.javac.version tool.javac.sha256 \
            tool.git.path tool.git.version tool.git.sha256 \
            tool.fish.path tool.fish.version tool.fish.sha256 \
            tool.jq.path tool.jq.version tool.jq.sha256 \
            tool.xmlstarlet.path tool.xmlstarlet.resolved_path tool.xmlstarlet.sha256 \
            tool.xmlstarlet.version_output_sha256 tool.xmlstarlet.linked_library_versions_sha256 \
            tool.xmlstarlet.xpath_capability tool.bsdtar.path tool.bsdtar.version tool.bsdtar.sha256 \
            tool.android_command_line_tools.allowed_versions tool.android_command_line_tools.selected_root \
            tool.android_command_line_tools.selected_version \
            tool.apkanalyzer.path tool.apkanalyzer.version tool.apkanalyzer.sha256 \
            tool.sdkmanager.path tool.sdkmanager.version tool.sdkmanager.sha256 \
            tool.aapt2.path tool.aapt2.sha256 tool.apksigner.path tool.apksigner.sha256 \
            tool.zipalign.path tool.zipalign.sha256 tool.llvm_readelf.path tool.llvm_readelf.sha256 \
            tool.llvm_objdump.path tool.llvm_objdump.sha256 \
            android.native_cpu android.native_feature_floor android.release_optimization \
            libjxl.fingerprint_sha256 projucer.fingerprint_sha256 verification.pre_publish_sha256
            printf '%s=value\n' $shuba_key
        end
        printf '%s\n' \
            artifact.basename=$shuba_basename \
            artifact.sha256=$shuba_digest \
            artifact.bytes=$shuba_bytes \
            app.version_name=(shuba_contract_get app.version_name) \
            app.version_code=(shuba_contract_get app.version_code) \
            signing.certificate_sha256=(shuba_contract_get signing.certificate_sha256) \
            release_state_begin \
            release_state_schema_version=1 \
            root_commit=$shuba_root_commit \
            release_state_end \
            submodule_state_begin \
            submodule_state_end \
            build_input_manifest_begin \
            build_input_manifest_end
    end >$shuba_path
    chmod 0644 -- $shuba_path
end

function shuba_publication_assets_write_packet --argument-names shuba_directory shuba_root_commit
    mkdir -p -- $shuba_directory; or return 1
    set --local shuba_basename (shuba_contract_get artifact.basename); or return 1
    printf '%s\n' 'accepted packet fixture bytes' >$shuba_directory/$shuba_basename; or return 1
    chmod 0644 -- $shuba_directory/$shuba_basename; or return 1
    set --local shuba_digest (shuba_sha256_file $shuba_directory/$shuba_basename); or return 1
    shuba_write_checksum_sidecar $shuba_directory/$shuba_basename.sha256 $shuba_digest $shuba_basename; or return 1
    shuba_publication_assets_write_packet_provenance $shuba_directory/$shuba_basename.provenance.txt \
        $shuba_basename $shuba_digest (stat --format %s -- $shuba_directory/$shuba_basename) $shuba_root_commit; or return 1
    printf '%s\n' 'verification evidence fixture' >$shuba_directory/$shuba_basename.verification.txt; or return 1
    chmod 0644 -- $shuba_directory/$shuba_basename.verification.txt
end

function shuba_validate_artifact_directory --argument-names shuba_directory shuba_basename shuba_verifier
    if not test -d $shuba_directory; or test -L $shuba_directory
        shuba_fail 'test packet directory is unavailable'
        return 1
    end
    set --local shuba_expected (shuba_artifact_expected_names $shuba_basename)
    set --local shuba_actual (find -P $shuba_directory -mindepth 1 -maxdepth 1 -printf '%f\n' | sort)
    if test (string join , -- $shuba_expected) != (string join , -- $shuba_actual)
        shuba_fail 'test packet inventory is invalid'
        return 1
    end
    for shuba_name in $shuba_expected
        shuba_require_regular_file $shuba_directory/$shuba_name; or return 1
        if test (stat --format %a -- $shuba_directory/$shuba_name) != 644
            shuba_fail 'test packet mode is invalid'
            return 1
        end
    end
    shuba_verify_checksum_sidecar $shuba_directory/$shuba_basename.sha256 \
        $shuba_directory/$shuba_basename $shuba_basename
end

function shuba_publication_assets_test_source --argument-names shuba_project
    set --local shuba_outputs $shuba_publication_assets_root/source-outputs
    mkdir -p -- $shuba_outputs; or return 1
    shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/first; or return 1
    shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/second; or return 1
    set --local shuba_stem (shuba_exact_source_stem); or return 1
    for shuba_name in (shuba_exact_source_expected_names $shuba_stem)
        cmp --silent $shuba_outputs/first/$shuba_name $shuba_outputs/second/$shuba_name; or begin
            shuba_publication_assets_test_fail "source rebuild changed $shuba_name"
            return 1
        end
    end
    shuba_exact_source_verify $shuba_project v1.0.2 $shuba_outputs/first false; or return 1
    set --local shuba_inventory $shuba_outputs/first/$shuba_stem.inventory.txt
    grep --fixed-strings --line-regexp --quiet \
        'symlink|777|source.txt|Shuba-1.0.2-source/source-link.txt' $shuba_inventory; or begin
        shuba_publication_assets_test_fail 'safe symlink to a packaged regular file was not retained as a symlink'
        return 1
    end
    if grep --extended-regexp --quiet \
            '^(file|directory)\|[^|]+\|[^|]+\|Shuba-1[.]0[.]2-source/source-link[.]txt$' $shuba_inventory
        shuba_publication_assets_test_fail 'safe symlink was misclassified through its packaged target'
        return 1
    end

    cp -a -- $shuba_outputs/first $shuba_outputs/checksum
    printf '%s\n' '0000000000000000000000000000000000000000000000000000000000000000  '$shuba_stem.tar.zst >$shuba_outputs/checksum/$shuba_stem.tar.zst.sha256
    shuba_publication_assets_expect_rejection source-checksum 'checksum sidecar does not match' \
        shuba_exact_source_verify $shuba_project v1.0.2 $shuba_outputs/checksum false; or return 1

    cp -a -- $shuba_outputs/first $shuba_outputs/extra
    printf '%s\n' forbidden >$shuba_outputs/extra/extra
    shuba_publication_assets_expect_rejection source-extra-asset 'unexpected inventory' \
        shuba_exact_source_verify $shuba_project v1.0.2 $shuba_outputs/extra false; or return 1

    cp -a -- $shuba_outputs/first $shuba_outputs/inventory
    printf '%s\n' 'file|644|0000000000000000000000000000000000000000000000000000000000000000|Shuba-1.0.2-source/source.txt' >>$shuba_outputs/inventory/$shuba_stem.inventory.txt
    shuba_publication_assets_expect_rejection source-inventory 'source inventory differs' \
        shuba_exact_source_verify $shuba_project v1.0.2 $shuba_outputs/inventory false; or return 1

    cp -a -- $shuba_outputs/first $shuba_outputs/archive
    printf '%s\n' changed >>$shuba_outputs/archive/$shuba_stem.tar.zst
    shuba_exact_source_write_checksum $shuba_outputs/archive/$shuba_stem.tar.zst.sha256 \
        $shuba_outputs/archive/$shuba_stem.tar.zst $shuba_stem.tar.zst; or return 1
    shuba_publication_assets_expect_rejection source-archive-mutation 'source archive bytes' \
        shuba_exact_source_verify $shuba_project v1.0.2 $shuba_outputs/archive false; or return 1

    printf '%s\n' dirty >>$shuba_project/source.txt
    shuba_publication_assets_expect_rejection dirty-root 'project root checkout is dirty' \
        shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/dirty; or return 1
    git -C $shuba_project checkout -- source.txt; or return 1

    set --local shuba_dependency $shuba_project/third_party/dependency
    printf '%s\n' dirty >>$shuba_dependency/dependency.txt
    shuba_publication_assets_expect_rejection dirty-submodule 'project root checkout is dirty' \
        shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/dirty-submodule; or return 1
    git -C $shuba_dependency checkout -- dependency.txt; or return 1

    set --local shuba_recorded_dependency_commit \
        (git -C $shuba_project rev-parse HEAD:third_party/dependency); or return 1
    git -C $shuba_dependency checkout --detach HEAD^ >/dev/null 2>&1; or return 1
    shuba_publication_assets_expect_rejection wrong-submodule-commit 'project root checkout is dirty' \
        shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/wrong-submodule; or return 1
    git -C $shuba_dependency checkout --detach $shuba_recorded_dependency_commit >/dev/null 2>&1; or return 1

    rm -rf -- $shuba_dependency; or return 1
    shuba_publication_assets_expect_rejection missing-submodule 'project root checkout is dirty' \
        shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/missing-submodule; or return 1
    git -C $shuba_project -c protocol.file.allow=always submodule update --init --recursive >/dev/null 2>&1; or return 1

    ln -s /etc/passwd $shuba_project/absolute-link; or return 1
    shuba_publication_assets_commit $shuba_project unsafe-link; or return 1
    git -C $shuba_project tag --delete v1.0.2 >/dev/null; or return 1
    env GIT_COMMITTER_DATE='2026-08-28T00:00:00Z' GIT_AUTHOR_DATE='2026-08-28T00:00:00Z' \
        git -C $shuba_project tag --annotate v1.0.2 --message 'unsafe source fixture'; or return 1
    shuba_publication_assets_expect_rejection absolute-symlink 'symbolic link is absolute' \
        shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/unsafe; or return 1
end

function shuba_publication_assets_test_bundle --argument-names shuba_project
    git -C $shuba_project reset --hard --quiet HEAD^; or return 1
    git -C $shuba_project tag --delete v1.0.2 >/dev/null; or return 1
    env GIT_COMMITTER_DATE='2026-08-28T00:00:00Z' GIT_AUTHOR_DATE='2026-08-28T00:00:00Z' \
        git -C $shuba_project tag --annotate v1.0.2 --message 'Shuba 1.0.2 publication fixture'; or return 1
    git -C $shuba_project -c protocol.file.allow=always submodule update --init --recursive >/dev/null 2>&1; or return 1
    set --local shuba_outputs $shuba_publication_assets_root/bundle-outputs
    mkdir -p -- $shuba_outputs; or return 1
    shuba_exact_source_build $shuba_project v1.0.2 $shuba_outputs/source; or return 1
    set --local shuba_commit (git -C $shuba_project rev-parse v1.0.2^{}); or return 1
    shuba_publication_assets_write_packet $shuba_outputs/packet $shuba_commit; or return 1
    printf '%s\n' '# Fixture release notes' 'No private material.' >$shuba_outputs/release-notes.md; or return 1
    chmod 0644 -- $shuba_outputs/release-notes.md; or return 1
    shuba_publication_bundle_build $shuba_project v1.0.2 $shuba_outputs/packet $shuba_outputs/source \
        $shuba_outputs/release-notes.md $shuba_outputs/bundle; or return 1
    shuba_publication_bundle_verify $shuba_project v1.0.2 $shuba_outputs/bundle; or return 1
    set --local shuba_stem (shuba_publication_bundle_stem); or return 1
    set --local shuba_apk_name (shuba_contract_get artifact.basename); or return 1
    set --local shuba_expected_names (shuba_publication_bundle_expected_names $shuba_stem $shuba_apk_name)
    for shuba_name in $shuba_expected_names
        test (stat --format %a -- $shuba_outputs/bundle/$shuba_name) = 644; or begin
            shuba_publication_assets_test_fail "bundle output mode is not public-safe: $shuba_name"
            return 1
        end
    end
    set --local shuba_manifest $shuba_outputs/bundle/$shuba_stem-SHA256SUMS
    set --local shuba_manifest_names (awk '{ print $2 }' $shuba_manifest | sort); or return 1
    set --local shuba_expected_manifest_names
    for shuba_name in $shuba_expected_names
        if not string match --quiet '*-SHA256SUMS' -- $shuba_name
            set --append shuba_expected_manifest_names $shuba_name
        end
    end
    set shuba_expected_manifest_names (printf '%s\n' $shuba_expected_manifest_names | sort)
    if test (string join , -- $shuba_manifest_names) != (string join , -- $shuba_expected_manifest_names)
        shuba_publication_assets_test_fail 'bundle manifest does not name every other public asset exactly once'
        return 1
    end
    if test (wc --lines <$shuba_manifest) -ne (math (count $shuba_expected_names) - 1)
        shuba_publication_assets_test_fail 'bundle manifest row count differs from the non-manifest asset count'
        return 1
    end
    cmp --silent $shuba_outputs/packet/$shuba_apk_name $shuba_outputs/bundle/$shuba_apk_name; or return 1
    cmp --silent $shuba_outputs/source/$shuba_stem-source.tar.zst $shuba_outputs/bundle/$shuba_stem-source.tar.zst; or return 1

    cp -a -- $shuba_outputs/bundle $shuba_outputs/apk-mutation
    printf '%s\n' mutation >>$shuba_outputs/apk-mutation/$shuba_apk_name
    shuba_publication_assets_expect_rejection bundle-apk-mutation 'checksum sidecar does not match' \
        shuba_publication_bundle_verify $shuba_project v1.0.2 $shuba_outputs/apk-mutation; or return 1

    cp -a -- $shuba_outputs/bundle $shuba_outputs/provenance-mismatch
    sed -i 's/^root_commit=.*/root_commit=0000000000000000000000000000000000000000/' \
        $shuba_outputs/provenance-mismatch/$shuba_apk_name.provenance.txt
    shuba_publication_assets_expect_rejection bundle-provenance-mismatch 'root commit differs' \
        shuba_publication_bundle_verify $shuba_project v1.0.2 $shuba_outputs/provenance-mismatch; or return 1

    cp -a -- $shuba_outputs/bundle $shuba_outputs/extra
    printf '%s\n' forbidden >$shuba_outputs/extra/extra
    shuba_publication_assets_expect_rejection bundle-extra-asset 'unexpected public-asset inventory' \
        shuba_publication_bundle_verify $shuba_project v1.0.2 $shuba_outputs/extra; or return 1

    cp -a -- $shuba_outputs/bundle $shuba_outputs/notes
    printf '%s\n' 'SHUBA_ANDROID_STORE_PASSWORD=forbidden' >>$shuba_outputs/notes/$shuba_stem-release-notes.md
    shuba_publication_assets_expect_rejection bundle-private-notes 'contains private material' \
        shuba_publication_bundle_verify $shuba_project v1.0.2 $shuba_outputs/notes; or return 1

    cp -a -- $shuba_outputs/bundle $shuba_outputs/manifest
    sed -i '1s/^[0-9a-f]/0/' $shuba_outputs/manifest/$shuba_stem-SHA256SUMS
    shuba_publication_assets_expect_rejection bundle-manifest 'overall SHA-256 manifest' \
        shuba_publication_bundle_verify $shuba_project v1.0.2 $shuba_outputs/manifest; or return 1
end

function shuba_publication_assets_test_main
    set --global shuba_publication_assets_workspace \
        (realpath --canonicalize-existing -- (status dirname)/../../..); or return 1
    set --global shuba_publication_assets_root \
        (mktemp --directory /tmp/shuba-r12f-publication-assets.XXXXXX); or return 1
    shuba_contract_load $shuba_publication_assets_workspace/release/release.properties; or return 1
    set --local shuba_project (shuba_publication_assets_make_project); or return 1
    shuba_publication_assets_test_tag_notes $shuba_project; or return 1
    shuba_publication_assets_test_source $shuba_project; or return 1
    shuba_publication_assets_test_bundle $shuba_project; or return 1
    printf '%s\n' 'R12F deterministic corresponding-source and public-bundle mutation probes: passed'
end

set --global shuba_publication_assets_root ''
function shuba_publication_assets_test_cleanup --on-event fish_exit
    if test -n "$shuba_publication_assets_root"; and test -d $shuba_publication_assets_root
        rm -rf -- $shuba_publication_assets_root
    end
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/release-contract.fish
source $shuba_script_directory/../lib/atomic-publication.fish
source $shuba_script_directory/../lib/release-artifact.fish
source $shuba_script_directory/../lib/exact-source.fish
source $shuba_script_directory/../lib/publication-bundle.fish

shuba_publication_assets_test_main
set --local shuba_main_status $status
exit $shuba_main_status
