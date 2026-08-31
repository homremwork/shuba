function shuba_exact_source_fail
    shuba_fail 'exact source:' $argv
end

function shuba_exact_source_stem
    set --local shuba_app_name (shuba_contract_get app.name); or return 1
    set shuba_app_name (string replace --all ' ' - -- $shuba_app_name)
    set --local shuba_version (shuba_contract_get app.version_name); or return 1
    printf '%s-%s-source\n' $shuba_app_name $shuba_version
end

function shuba_exact_source_top_directory
    shuba_exact_source_stem
end

function shuba_exact_source_expected_names --argument-names shuba_stem
    printf '%s\n' \
        $shuba_stem.tar.zst \
        $shuba_stem.tar.zst.sha256 \
        $shuba_stem.inventory.txt \
        $shuba_stem.submodules.txt | sort
end

function shuba_exact_source_validate_component --argument-names shuba_component
    if test -z "$shuba_component"; or test "$shuba_component" = .; or test "$shuba_component" = ..
        shuba_exact_source_fail 'source path has an empty, dot, or traversal component'
        return 1
    end
    if string match --regex --quiet '[[:cntrl:]|/]' -- $shuba_component
        shuba_exact_source_fail 'source path has a control character, inventory separator, or slash in a component'
        return 1
    end
    if test "$shuba_component" = .git
        shuba_exact_source_fail 'source tree must not contain Git metadata'
        return 1
    end
    return 0
end

function shuba_exact_source_validate_archive_path --argument-names shuba_path shuba_top
    if not string match --quiet "$shuba_top"'/*' -- $shuba_path; and test "$shuba_path" != "$shuba_top"
        shuba_exact_source_fail "source archive entry escapes its required top-level directory: $shuba_path"
        return 1
    end
    if string match --regex --quiet '[[:cntrl:]|]' -- $shuba_path
        shuba_exact_source_fail 'source archive path has a control character or inventory separator'
        return 1
    end
    for shuba_component in (string split / -- $shuba_path)
        shuba_exact_source_validate_component $shuba_component; or return 1
    end
    return 0
end

function shuba_exact_source_validate_public_path --argument-names shuba_path shuba_top
    shuba_exact_source_validate_archive_path $shuba_path $shuba_top; or return 1
    set --local shuba_relative (string replace -- "$shuba_top/" '' $shuba_path)
    if test "$shuba_relative" = "$shuba_top"
        set shuba_relative ''
    end
    switch $shuba_relative
        case .git '.git/*'
            shuba_exact_source_fail 'source tree must not contain Git metadata'
            return 1
        case build 'build/*' dist 'dist/*' JuceLibraryCode 'JuceLibraryCode/*' Builds/Android 'Builds/Android/*'
            shuba_exact_source_fail "source tree contains a prohibited generated or release output: $shuba_relative"
            return 1
    end

    set --local shuba_filename (string lower -- (path basename -- $shuba_path))
    switch $shuba_filename
        case '*.p12' '*.pfx' '*.jks' '*.keystore' '*.pem' '*.key' '*.der' '*.kdb' '*.kdbx' '.env' 'local.properties'
            shuba_exact_source_fail "source tree contains prohibited signing or local-configuration material: $shuba_path"
            return 1
    end
    return 0
end

function shuba_exact_source_validate_link_target --argument-names shuba_path shuba_target shuba_top
    if test -z "$shuba_target"; or string match --regex --quiet '[[:cntrl:]|]' -- $shuba_target
        shuba_exact_source_fail "source symbolic link has an empty or non-text-safe target: $shuba_path"
        return 1
    end
    if string match --quiet '/*' -- $shuba_target
        shuba_exact_source_fail "source symbolic link is absolute: $shuba_path"
        return 1
    end

    set --local shuba_resolved_components (string split / -- (path dirname -- $shuba_path))
    for shuba_component in (string split / -- $shuba_target)
        switch $shuba_component
            case '' .
                continue
            case ..
                if test (count $shuba_resolved_components) -le 1
                    shuba_exact_source_fail "source symbolic link escapes the archive root: $shuba_path"
                    return 1
                end
                set --erase shuba_resolved_components[-1]
            case '*'
                shuba_exact_source_validate_component $shuba_component; or return 1
                set --append shuba_resolved_components $shuba_component
        end
    end
    if test "$shuba_resolved_components[1]" != "$shuba_top"
        shuba_exact_source_fail "source symbolic link escapes the archive root: $shuba_path"
        return 1
    end
    return 0
end

function shuba_exact_source_validate_tree_object_path --argument-names shuba_relative_path
    if test -z "$shuba_relative_path"; or string match --quiet '/*' -- $shuba_relative_path
        shuba_exact_source_fail 'Git tree contains an empty or absolute path'
        return 1
    end
    for shuba_component in (string split / -- $shuba_relative_path)
        shuba_exact_source_validate_component $shuba_component; or return 1
    end
    return 0
end

function shuba_exact_source_validate_relative_source_path --argument-names shuba_relative_path
    shuba_exact_source_validate_tree_object_path $shuba_relative_path; or return 1
    switch $shuba_relative_path
        case .git '.git/*'
            shuba_exact_source_fail 'source tree must not contain Git metadata'
            return 1
        case build 'build/*' dist 'dist/*' JuceLibraryCode 'JuceLibraryCode/*' Builds/Android 'Builds/Android/*'
            shuba_exact_source_fail "source tree contains a prohibited generated or release output: $shuba_relative_path"
            return 1
    end
    set --local shuba_filename (string lower -- (path basename -- $shuba_relative_path))
    switch $shuba_filename
        case '*.p12' '*.pfx' '*.jks' '*.keystore' '*.pem' '*.key' '*.der' '*.kdb' '*.kdbx' '.env' 'local.properties'
            shuba_exact_source_fail "source tree contains prohibited signing or local-configuration material: $shuba_relative_path"
            return 1
    end
    return 0
end

function shuba_exact_source_validate_checkout --argument-names shuba_repository shuba_expected_commit shuba_label shuba_require_clean shuba_require_recursive_clean
    if not test -d $shuba_repository; or test -L $shuba_repository
        shuba_exact_source_fail "$shuba_label checkout is unavailable or symbolic-linked: $shuba_repository"
        return 1
    end
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    if test ($shuba_git_path -C $shuba_repository rev-parse --is-inside-work-tree) != true
        shuba_exact_source_fail "$shuba_label is not a Git worktree: $shuba_repository"
        return 1
    end
    set --local shuba_head ($shuba_git_path -C $shuba_repository rev-parse HEAD); or return 1
    if test "$shuba_head" != "$shuba_expected_commit"
        shuba_exact_source_fail "$shuba_label HEAD differs from the recorded source commit"
        return 1
    end
    if test "$shuba_require_clean" = true
        set --local shuba_ignore_submodules all
        if test "$shuba_require_recursive_clean" = true
            set shuba_ignore_submodules none
        end
        set --local shuba_status ($shuba_git_path -C $shuba_repository status --porcelain=v1 --untracked-files=all --ignore-submodules=$shuba_ignore_submodules); or return 1
        if test (count $shuba_status) -ne 0
            shuba_exact_source_fail "$shuba_label checkout is dirty"
            return 1
        end
    end
    $shuba_git_path -C $shuba_repository cat-file -e "$shuba_expected_commit^{tree}"; or begin
        shuba_exact_source_fail "$shuba_label does not contain its recorded Git tree"
        return 1
    end
    return 0
end

function shuba_exact_source_resolve_tag --argument-names shuba_project_root shuba_tag
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    set --local shuba_expected_tag v(shuba_contract_get app.version_name); or return 1
    if test "$shuba_tag" != "$shuba_expected_tag"
        shuba_exact_source_fail "tag must match the release contract exactly: $shuba_expected_tag"
        return 1
    end
    set --local shuba_ref refs/tags/$shuba_tag
    set --local shuba_object_type ($shuba_git_path -C $shuba_project_root for-each-ref --format='%(objecttype)' $shuba_ref); or return 1
    if test (count $shuba_object_type) -ne 1; or test "$shuba_object_type" != tag
        shuba_exact_source_fail "release tag must exist and be annotated: $shuba_tag"
        return 1
    end
    set --local shuba_tag_body ($shuba_git_path -C $shuba_project_root cat-file tag $shuba_ref | string collect --no-trim-newlines); or return 1
    if string match --quiet '*-----BEGIN PGP SIGNATURE-----*' -- $shuba_tag_body; or string match --quiet '*-----BEGIN SSH SIGNATURE-----*' -- $shuba_tag_body
        shuba_exact_source_fail "release tag must be unsigned: $shuba_tag"
        return 1
    end
    set --local shuba_target ($shuba_git_path -C $shuba_project_root rev-parse "$shuba_tag^{}"); or return 1
    if test ($shuba_git_path -C $shuba_project_root cat-file -t $shuba_target) != commit
        shuba_exact_source_fail "annotated release tag must point directly to a commit: $shuba_tag"
        return 1
    end
    set --local shuba_tagger_timestamp ($shuba_git_path -C $shuba_project_root for-each-ref --format='%(taggerdate:unix)' $shuba_ref); or return 1
    if test (count $shuba_tagger_timestamp) -ne 1; or not string match --regex --quiet '^[0-9]+$' -- $shuba_tagger_timestamp
        shuba_exact_source_fail "release tag has no usable non-negative tagger timestamp: $shuba_tag"
        return 1
    end
    set --global shuba_exact_source_tag $shuba_tag
    set --global shuba_exact_source_commit $shuba_target
    set --global shuba_exact_source_tagger_timestamp $shuba_tagger_timestamp
    set --global shuba_exact_source_stem_name (shuba_exact_source_stem); or return 1
    set --global shuba_exact_source_top_name (shuba_exact_source_top_directory); or return 1
end

function shuba_exact_source_write_tag_notes --argument-names shuba_project_root shuba_tag shuba_requested_path
    shuba_exact_source_resolve_tag $shuba_project_root $shuba_tag; or return 1
    set --local shuba_parent (realpath --canonicalize-existing -- (path dirname -- $shuba_requested_path)); or begin
        shuba_exact_source_fail "release-notes output parent is unavailable: $shuba_requested_path"
        return 1
    end
    set --local shuba_name (path basename -- $shuba_requested_path)
    shuba_exact_source_validate_component $shuba_name; or return 1
    set --local shuba_output $shuba_parent/$shuba_name
    if test -d $shuba_output; or test -L $shuba_output
        shuba_exact_source_fail 'release-notes output must not be a directory or symbolic link'
        return 1
    end
    set --local shuba_notes (mktemp $shuba_parent/.shuba-tag-notes.XXXXXX); or return 1
    set --local shuba_git_path (shuba_resolve_command git); or begin
        rm -f -- $shuba_notes
        return 1
    end
    $shuba_git_path -C $shuba_project_root cat-file tag refs/tags/$shuba_tag \
        | sed '1,/^$/d' >$shuba_notes
    set --local shuba_extract_statuses $pipestatus
    for shuba_status in $shuba_extract_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_notes
            shuba_exact_source_fail 'could not extract the annotated release-tag message'
            return 1
        end
    end
    if not grep --quiet '[^[:space:]]' $shuba_notes
        rm -f -- $shuba_notes
        shuba_exact_source_fail 'annotated release tag must contain non-empty release notes'
        return 1
    end
    chmod 0644 -- $shuba_notes; or begin
        rm -f -- $shuba_notes
        return 1
    end
    mv --force -- $shuba_notes $shuba_output; or begin
        rm -f -- $shuba_notes
        return 1
    end
end

function shuba_exact_source_validate_git_tree --argument-names shuba_repository shuba_commit shuba_project_root shuba_source_prefix shuba_rows_path
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    set --local shuba_records (mktemp /tmp/shuba-exact-source-tree.XXXXXX); or return 1
    set --local shuba_tab (printf '\t')
    $shuba_git_path -C $shuba_repository ls-tree --full-tree -r -z $shuba_commit >$shuba_records; or begin
        rm -f -- $shuba_records
        return 1
    end
    while read --null shuba_record
        set --local shuba_record_parts (string split --max 1 $shuba_tab -- $shuba_record)
        if test (count $shuba_record_parts) -ne 2
            rm -f -- $shuba_records
            shuba_exact_source_fail 'could not parse a NUL-delimited Git tree entry'
            return 1
        end
        set --local shuba_header (string split ' ' -- $shuba_record_parts[1])
        if test (count $shuba_header) -ne 3
            rm -f -- $shuba_records
            shuba_exact_source_fail 'Git tree entry has an invalid header'
            return 1
        end
        set --local shuba_mode $shuba_header[1]
        set --local shuba_type $shuba_header[2]
        set --local shuba_object $shuba_header[3]
        set --local shuba_relative_path $shuba_record_parts[2]
        shuba_exact_source_validate_relative_source_path $shuba_relative_path; or begin
            rm -f -- $shuba_records
            return 1
        end
        set --local shuba_full_path $shuba_relative_path
        if test -n "$shuba_source_prefix"
            set shuba_full_path $shuba_source_prefix/$shuba_relative_path
        end
        switch $shuba_type
            case blob
                if not contains -- $shuba_mode 100644 100755 120000
                    rm -f -- $shuba_records
                    shuba_exact_source_fail "Git blob has an unsupported mode: $shuba_full_path"
                    return 1
                end
                if test $shuba_mode = 120000
                    set --local shuba_link_target ($shuba_git_path -C $shuba_repository cat-file blob $shuba_object | string collect --no-trim-newlines); or begin
                        rm -f -- $shuba_records
                        return 1
                    end
                    set --local shuba_package_path $shuba_exact_source_top_name/$shuba_full_path
                    shuba_exact_source_validate_link_target $shuba_package_path $shuba_link_target $shuba_exact_source_top_name; or begin
                        rm -f -- $shuba_records
                        return 1
                    end
                end
            case commit
                if test $shuba_mode != 160000; or not string match --regex --quiet '^[0-9a-f]{40,64}$' -- $shuba_object
                    rm -f -- $shuba_records
                    shuba_exact_source_fail "Git submodule entry is malformed: $shuba_full_path"
                    return 1
                end
                if not string match --regex --quiet '^third_party/[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*$' -- $shuba_full_path
                    rm -f -- $shuba_records
                    shuba_exact_source_fail "Git submodule path is unsafe or outside third_party: $shuba_full_path"
                    return 1
                end
                printf 'submodule|%s|commit=%s\n' $shuba_full_path $shuba_object >>$shuba_rows_path; or begin
                    rm -f -- $shuba_records
                    return 1
                end
            case '*'
                rm -f -- $shuba_records
                shuba_exact_source_fail "Git tree has an unsupported object type: $shuba_full_path"
                return 1
        end
    end <$shuba_records
    set --local shuba_tree_status $status
    rm -f -- $shuba_records
    return $shuba_tree_status
end

function shuba_exact_source_export_repository --argument-names shuba_repository shuba_commit shuba_project_root shuba_source_prefix shuba_destination shuba_rows_path shuba_require_clean shuba_require_recursive_clean
    shuba_exact_source_validate_checkout $shuba_repository $shuba_commit "source repository $shuba_source_prefix" $shuba_require_clean $shuba_require_recursive_clean; or return 1
    shuba_exact_source_validate_git_tree $shuba_repository $shuba_commit $shuba_project_root $shuba_source_prefix $shuba_rows_path; or return 1
    mkdir -p -- $shuba_destination; or return 1
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    set --local shuba_tar_path (shuba_resolve_command bsdtar); or return 1
    set --local shuba_tar_version ($shuba_tar_path --version 2>&1); or begin
        shuba_exact_source_fail 'could not query bsdtar version for source archive construction'
        return 1
    end
    set --local shuba_tar_semantic_version (string match --regex '[0-9]+[.][0-9]+[.][0-9]+' -- $shuba_tar_version | head --lines 1)
    if test (count $shuba_tar_semantic_version) -ne 1; or not shuba_version_at_least $shuba_tar_semantic_version (shuba_contract_get tool.bsdtar_min_version)
        shuba_exact_source_fail 'bsdtar does not satisfy the source-archive version contract'
        return 1
    end
    $shuba_git_path -C $shuba_repository archive --format=tar $shuba_commit \
        | $shuba_tar_path -x --no-same-owner --no-same-permissions --no-xattrs --no-acls --no-fflags -f - -C $shuba_destination
    set --local shuba_export_statuses $pipestatus
    for shuba_status in $shuba_export_statuses
        if test $shuba_status -ne 0
            shuba_exact_source_fail "could not export Git object data for $shuba_source_prefix"
            return 1
        end
    end

    set --local shuba_links (mktemp /tmp/shuba-exact-source-links.XXXXXX); or return 1
    awk -F'|' -v prefix="$shuba_source_prefix" '
        $1 == "submodule" {
            path = $2
            if (prefix == "") {
                print path "|" $3
                next
            }
            prefix_with_slash = prefix "/"
            if (index(path, prefix_with_slash) == 1) {
                descendant = substr(path, length(prefix_with_slash) + 1)
                if (index(descendant, "/") == 0) {
                    print path "|" $3
                }
            }
        }
    ' $shuba_rows_path | sort --unique >$shuba_links
    while read --local shuba_link
        set --local shuba_link_fields (string split --max 1 '|' -- $shuba_link)
        if test (count $shuba_link_fields) -ne 2; or not string match --quiet 'commit=*' -- $shuba_link_fields[2]
            rm -f -- $shuba_links
            shuba_exact_source_fail 'recursive submodule map has an invalid record'
            return 1
        end
        set --local shuba_link_path $shuba_link_fields[1]
        set --local shuba_child_commit (string replace 'commit=' '' -- $shuba_link_fields[2])
        set --local shuba_relative_child $shuba_link_path
        if test -n "$shuba_source_prefix"
            set shuba_relative_child (string replace -- "$shuba_source_prefix/" '' $shuba_link_path)
        end
        if contains -- $shuba_link_path $shuba_exact_source_seen_submodule_paths
            rm -f -- $shuba_links
            shuba_exact_source_fail "recursive submodule map repeats a path: $shuba_link_path"
            return 1
        end
        set --append shuba_exact_source_seen_submodule_paths $shuba_link_path
        shuba_exact_source_export_repository $shuba_project_root/$shuba_link_path $shuba_child_commit \
            $shuba_project_root $shuba_link_path $shuba_destination/$shuba_relative_child $shuba_rows_path false false; or begin
            rm -f -- $shuba_links
            return 1
        end
    end <$shuba_links
    set --local shuba_links_status $status
    rm -f -- $shuba_links
    return $shuba_links_status
end

function shuba_exact_source_normalize_tree --argument-names shuba_tree
    find -P $shuba_tree -type d -exec chmod 0755 {} +; or return 1
    set --local shuba_paths (mktemp /tmp/shuba-exact-source-files.XXXXXX); or return 1
    find -P $shuba_tree -type f -print0 >$shuba_paths; or begin
        rm -f -- $shuba_paths
        return 1
    end
    while read --null shuba_path
        if test -x $shuba_path
            chmod 0755 -- $shuba_path
        else
            chmod 0644 -- $shuba_path
        end
        if test $status -ne 0
            rm -f -- $shuba_paths
            return 1
        end
    end <$shuba_paths
    set --local shuba_normalize_status $status
    rm -f -- $shuba_paths
    if test $shuba_normalize_status -ne 0
        return $shuba_normalize_status
    end
    find -P $shuba_tree -exec touch --no-dereference --date=@$shuba_exact_source_tagger_timestamp -- {} +
end

function shuba_exact_source_validate_materialized_tree --argument-names shuba_parent shuba_top
    set --local shuba_tree $shuba_parent/$shuba_top
    if not test -d $shuba_tree; or test -L $shuba_tree
        shuba_exact_source_fail "source package root is unavailable or symbolic-linked: $shuba_top"
        return 1
    end
    for shuba_required_path in LICENSE THIRD_PARTY_NOTICES.md .gitmodules
        shuba_require_regular_file $shuba_tree/$shuba_required_path; or begin
            shuba_exact_source_fail "source package lacks required public licensing or submodule metadata: $shuba_required_path"
            return 1
        end
    end
    set --local shuba_unexpected (find -P $shuba_tree ! -type d ! -type f ! -type l -print -quit)
    if test $status -ne 0; or test -n "$shuba_unexpected"
        shuba_exact_source_fail 'source package contains a device, socket, FIFO, or other unsupported entry'
        return 1
    end
    set --local shuba_paths (mktemp /tmp/shuba-exact-source-materialized.XXXXXX); or return 1
    find -P $shuba_tree -print0 | sort --zero-terminated >$shuba_paths; or begin
        rm -f -- $shuba_paths
        return 1
    end
    while read --null shuba_path
        set --local shuba_relative (string replace -- "$shuba_parent/" '' $shuba_path)
        shuba_exact_source_validate_public_path $shuba_relative $shuba_top; or begin
            rm -f -- $shuba_paths
            return 1
        end
        if test (stat --format %Y -- $shuba_path) != $shuba_exact_source_tagger_timestamp
            rm -f -- $shuba_paths
            shuba_exact_source_fail "source path does not use the release-tag timestamp: $shuba_relative"
            return 1
        end
        if test -L $shuba_path
            shuba_exact_source_validate_link_target $shuba_relative (readlink -- $shuba_path) $shuba_top; or begin
                rm -f -- $shuba_paths
                return 1
            end
        else if test -d $shuba_path
            if test (stat --format %a -- $shuba_path) != 755
                rm -f -- $shuba_paths
                shuba_exact_source_fail "source directory mode is not public-safe: $shuba_relative"
                return 1
            end
        else if test -f $shuba_path
            set --local shuba_mode (stat --format %a -- $shuba_path)
            if not contains -- $shuba_mode 644 755
                rm -f -- $shuba_paths
                shuba_exact_source_fail "source file mode is not public-safe: $shuba_relative"
                return 1
            end
        end
    end <$shuba_paths
    set --local shuba_path_status $status
    rm -f -- $shuba_paths
    if test $shuba_path_status -ne 0
        return $shuba_path_status
    end
    grep --recursive --binary-files=without-match --quiet --extended-regexp \
        -- '-----BEGIN( [A-Za-z0-9]+)* PRIVATE KEY-----' $shuba_tree
    set --local shuba_secret_status $status
    if test $shuba_secret_status -eq 0
        shuba_exact_source_fail 'source package contains private-key material'
        return 1
    else if test $shuba_secret_status -gt 1
        shuba_exact_source_fail 'could not inspect source package for private-key material'
        return 1
    end
    return 0
end

function shuba_exact_source_write_tree_inventory --argument-names shuba_inventory_path shuba_parent shuba_top
    shuba_exact_source_validate_materialized_tree $shuba_parent $shuba_top; or return 1
    set --local shuba_tree $shuba_parent/$shuba_top
    set --local shuba_paths (mktemp /tmp/shuba-exact-source-inventory.XXXXXX); or return 1
    set --local shuba_rows (mktemp /tmp/shuba-exact-source-inventory-rows.XXXXXX); or begin
        rm -f -- $shuba_paths
        return 1
    end
    find -P $shuba_tree -print0 | sort --zero-terminated >$shuba_paths; or begin
        rm -f -- $shuba_paths $shuba_rows
        return 1
    end
    while read --null shuba_path
        set --local shuba_relative (string replace -- "$shuba_parent/" '' $shuba_path)
        if test -L $shuba_path
            printf 'symlink|777|%s|%s\n' (readlink -- $shuba_path) $shuba_relative >>$shuba_rows
        else if test -d $shuba_path
            printf 'directory|%s|-|%s\n' (stat --format %a -- $shuba_path) $shuba_relative >>$shuba_rows
        else if test -f $shuba_path
            printf 'file|%s|%s|%s\n' (stat --format %a -- $shuba_path) \
                (shuba_sha256_file $shuba_path) $shuba_relative >>$shuba_rows
        else
            rm -f -- $shuba_paths $shuba_rows
            shuba_exact_source_fail 'source package inventory found an unsupported entry'
            return 1
        end
    end <$shuba_paths
    begin
        printf 'exact_source_inventory_schema_version=1\n'
        sort -- $shuba_rows
    end >$shuba_inventory_path
    set --local shuba_inventory_status $status
    rm -f -- $shuba_paths $shuba_rows
    return $shuba_inventory_status
end

function shuba_exact_source_write_submodule_manifest --argument-names shuba_manifest_path shuba_rows_path
    if not test -f $shuba_rows_path; or test -L $shuba_rows_path
        shuba_exact_source_fail 'recursive submodule rows are unavailable'
        return 1
    end
    set --local shuba_duplicate (sort -- $shuba_rows_path | uniq --repeated | head --lines 1)
    if test -n "$shuba_duplicate"
        shuba_exact_source_fail 'recursive submodule map contains a duplicate record'
        return 1
    end
    begin
        printf 'exact_source_submodules_schema_version=1\n'
        printf 'release_tag=%s\n' $shuba_exact_source_tag
        printf 'root_commit=%s\n' $shuba_exact_source_commit
        printf 'tagger_timestamp=%s\n' $shuba_exact_source_tagger_timestamp
        sort -- $shuba_rows_path
    end >$shuba_manifest_path
end

function shuba_exact_source_prepare_tree --argument-names shuba_project_root shuba_tag shuba_parent
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_exact_source_resolve_tag $shuba_project_root $shuba_tag; or return 1
    shuba_exact_source_validate_checkout $shuba_project_root $shuba_exact_source_commit 'project root' true true; or return 1
    if test -e $shuba_parent/$shuba_exact_source_top_name
        shuba_exact_source_fail 'exact-source staging destination already exists'
        return 1
    end
    set --global shuba_exact_source_seen_submodule_paths
    set --local shuba_rows $shuba_parent/.submodule-rows
    printf '' >$shuba_rows; or return 1
    shuba_exact_source_export_repository $shuba_project_root $shuba_exact_source_commit $shuba_project_root '' \
        $shuba_parent/$shuba_exact_source_top_name $shuba_rows true true; or return 1
    shuba_exact_source_normalize_tree $shuba_parent/$shuba_exact_source_top_name; or return 1
    shuba_exact_source_validate_materialized_tree $shuba_parent $shuba_exact_source_top_name; or return 1
    set --global shuba_exact_source_rows_path $shuba_rows
end

function shuba_exact_source_write_checksum --argument-names shuba_checksum_path shuba_archive_path shuba_archive_name
    set --local shuba_digest (shuba_sha256_file $shuba_archive_path); or return 1
    printf '%s  %s\n' $shuba_digest $shuba_archive_name >$shuba_checksum_path; or return 1
    chmod 0644 -- $shuba_checksum_path
end

function shuba_exact_source_verify_checksum --argument-names shuba_checksum_path shuba_archive_path shuba_archive_name
    shuba_require_regular_file $shuba_checksum_path; or return 1
    shuba_require_regular_file $shuba_archive_path; or return 1
    set --local shuba_expected (printf '%s  %s\n' (shuba_sha256_file $shuba_archive_path) $shuba_archive_name | string collect)
    set --local shuba_actual (string collect <$shuba_checksum_path)
    if test "$shuba_actual" != "$shuba_expected"
        shuba_exact_source_fail 'source archive checksum sidecar does not match its bytes'
        return 1
    end
end

function shuba_exact_source_create_archive --argument-names shuba_archive_path shuba_parent
    set --local shuba_tar_path (shuba_resolve_command bsdtar); or return 1
    set --local shuba_archive_paths (mktemp /tmp/shuba-exact-source-archive-paths.XXXXXX); or return 1
    pushd $shuba_parent >/dev/null; or begin
        rm -f -- $shuba_archive_paths
        return 1
    end
    find -P $shuba_exact_source_top_name -print0 | sort --zero-terminated >$shuba_archive_paths
    set --local shuba_path_statuses $pipestatus
    popd >/dev/null; or begin
        rm -f -- $shuba_archive_paths
        return 1
    end
    for shuba_status in $shuba_path_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_archive_paths
            shuba_exact_source_fail 'could not create the deterministic source archive path list'
            return 1
        end
    end
    env LC_ALL=C TZ=UTC $shuba_tar_path -c --zstd --format=ustar --no-recursion --uid 0 --gid 0 --uname root --gname root \
        --no-xattrs --no-acls --no-fflags --null -C $shuba_parent -T $shuba_archive_paths \
        -f $shuba_archive_path
    set --local shuba_archive_status $status
    rm -f -- $shuba_archive_paths
    if test $shuba_archive_status -ne 0
        shuba_exact_source_fail 'could not create the deterministic Zstandard source archive'
        return $shuba_archive_status
    end
    chmod 0644 -- $shuba_archive_path
end

function shuba_exact_source_validate_output_directory --argument-names shuba_directory shuba_stem shuba_allow_extra_assets
    if not test -d $shuba_directory; or test -L $shuba_directory
        shuba_exact_source_fail "source asset directory is unavailable or symbolic-linked: $shuba_directory"
        return 1
    end
    set --local shuba_expected (mktemp /tmp/shuba-exact-source-expected.XXXXXX); or return 1
    set --local shuba_actual (mktemp /tmp/shuba-exact-source-actual.XXXXXX); or begin
        rm -f -- $shuba_expected
        return 1
    end
    shuba_exact_source_expected_names $shuba_stem >$shuba_expected
    find -P $shuba_directory -mindepth 1 -maxdepth 1 -printf '%f\n' | sort >$shuba_actual
    set --local shuba_inventory_statuses $pipestatus
    for shuba_status in $shuba_inventory_statuses
        if test $shuba_status -ne 0
            rm -f -- $shuba_expected $shuba_actual
            return 1
        end
    end
    set --local shuba_inventory_status 0
    if test "$shuba_allow_extra_assets" = true
        while read --local shuba_required_name
            if not grep --fixed-strings --line-regexp --quiet -- $shuba_required_name $shuba_actual
                set shuba_inventory_status 1
                break
            end
        end <$shuba_expected
    else
        cmp --silent $shuba_expected $shuba_actual
        set shuba_inventory_status $status
    end
    rm -f -- $shuba_expected $shuba_actual
    if test $shuba_inventory_status -ne 0
        shuba_exact_source_fail 'source asset directory has an unexpected inventory'
        return 1
    end
    for shuba_name in (shuba_exact_source_expected_names $shuba_stem)
        set --local shuba_path $shuba_directory/$shuba_name
        shuba_require_regular_file $shuba_path; or return 1
        if test (stat --format %a -- $shuba_path) != 644
            shuba_exact_source_fail "source asset file mode is not public-safe: $shuba_name"
            return 1
        end
    end
end

function shuba_exact_source_verify --argument-names shuba_project_root shuba_tag shuba_directory shuba_allow_extra_assets
    shuba_contract_load $shuba_project_root/release/release.properties; or return 1
    shuba_exact_source_resolve_tag $shuba_project_root $shuba_tag; or return 1
    shuba_exact_source_validate_output_directory $shuba_directory $shuba_exact_source_stem_name $shuba_allow_extra_assets; or return 1
    set --local shuba_archive_name $shuba_exact_source_stem_name.tar.zst
    set --local shuba_inventory_name $shuba_exact_source_stem_name.inventory.txt
    set --local shuba_manifest_name $shuba_exact_source_stem_name.submodules.txt
    shuba_exact_source_verify_checksum $shuba_directory/$shuba_archive_name.sha256 \
        $shuba_directory/$shuba_archive_name $shuba_archive_name; or return 1

    set --local shuba_expected_root (mktemp --directory /tmp/shuba-exact-source-verify-expected.XXXXXX); or return 1
    shuba_exact_source_prepare_tree $shuba_project_root $shuba_tag $shuba_expected_root; or begin
        rm -rf -- $shuba_expected_root
        return 1
    end
    set --local shuba_expected_inventory $shuba_expected_root/expected.inventory
    set --local shuba_expected_manifest $shuba_expected_root/expected.submodules
    shuba_exact_source_write_tree_inventory $shuba_expected_inventory $shuba_expected_root $shuba_exact_source_top_name; or begin
        rm -rf -- $shuba_expected_root
        return 1
    end
    shuba_exact_source_write_submodule_manifest $shuba_expected_manifest $shuba_exact_source_rows_path; or begin
        rm -rf -- $shuba_expected_root
        return 1
    end
    cmp --silent $shuba_expected_inventory $shuba_directory/$shuba_inventory_name; or begin
        rm -rf -- $shuba_expected_root
        shuba_exact_source_fail 'source inventory differs from the exact tagged recursive source graph'
        return 1
    end
    cmp --silent $shuba_expected_manifest $shuba_directory/$shuba_manifest_name; or begin
        rm -rf -- $shuba_expected_root
        shuba_exact_source_fail 'recursive submodule manifest differs from the exact tagged source graph'
        return 1
    end

    set --local shuba_expected_archive $shuba_expected_root/expected.tar.zst
    shuba_exact_source_create_archive $shuba_expected_archive $shuba_expected_root; or begin
        rm -rf -- $shuba_expected_root
        return 1
    end
    cmp --silent $shuba_expected_archive $shuba_directory/$shuba_archive_name; or begin
        rm -rf -- $shuba_expected_root
        shuba_exact_source_fail 'source archive bytes or normalized metadata differ from deterministic reconstruction'
        return 1
    end

    set --local shuba_tar_path (shuba_resolve_command bsdtar); or begin
        rm -rf -- $shuba_expected_root
        return 1
    end
    set --local shuba_tar_version ($shuba_tar_path --version 2>&1); or begin
        rm -rf -- $shuba_expected_root
        shuba_exact_source_fail 'could not query bsdtar version for source archive verification'
        return 1
    end
    set --local shuba_tar_semantic_version (string match --regex '[0-9]+[.][0-9]+[.][0-9]+' -- $shuba_tar_version | head --lines 1)
    if test (count $shuba_tar_semantic_version) -ne 1; or not shuba_version_at_least $shuba_tar_semantic_version (shuba_contract_get tool.bsdtar_min_version)
        rm -rf -- $shuba_expected_root
        shuba_exact_source_fail 'bsdtar does not satisfy the source-archive version contract'
        return 1
    end
    set --local shuba_archive_paths $shuba_expected_root/archive.paths
    set --local shuba_inventory_paths $shuba_expected_root/inventory.paths
    $shuba_tar_path -tf $shuba_directory/$shuba_archive_name \
        | sed -e 's|^\./||' -e 's|/$||' | sort >$shuba_archive_paths
    set --local shuba_list_statuses $pipestatus
    for shuba_status in $shuba_list_statuses
        if test $shuba_status -ne 0
            rm -rf -- $shuba_expected_root
            shuba_exact_source_fail 'could not list the source archive'
            return 1
        end
    end
    awk -F'|' 'NR > 1 { print $4 }' $shuba_directory/$shuba_inventory_name | sort >$shuba_inventory_paths
    cmp --silent $shuba_archive_paths $shuba_inventory_paths; or begin
        rm -rf -- $shuba_expected_root
        shuba_exact_source_fail 'source archive paths differ from the signed source inventory'
        return 1
    end
    set --local shuba_extracted_parent (mktemp --directory /tmp/shuba-exact-source-verify-extract.XXXXXX); or begin
        rm -rf -- $shuba_expected_root
        return 1
    end
    $shuba_tar_path -x --no-same-owner --no-same-permissions --no-xattrs --no-acls --no-fflags \
        -f $shuba_directory/$shuba_archive_name -C $shuba_extracted_parent; or begin
        rm -rf -- $shuba_expected_root $shuba_extracted_parent
        shuba_exact_source_fail 'could not safely extract the source archive for verification'
        return 1
    end
    set --local shuba_extracted_inventory $shuba_extracted_parent/extracted.inventory
    shuba_exact_source_write_tree_inventory $shuba_extracted_inventory $shuba_extracted_parent $shuba_exact_source_top_name; or begin
        rm -rf -- $shuba_expected_root $shuba_extracted_parent
        return 1
    end
    cmp --silent $shuba_extracted_inventory $shuba_directory/$shuba_inventory_name
    set --local shuba_extract_status $status
    rm -rf -- $shuba_expected_root $shuba_extracted_parent
    if test $shuba_extract_status -ne 0
        shuba_exact_source_fail 'source archive content or metadata differs from its inventory'
        return 1
    end
end

function shuba_exact_source_output_destination --argument-names shuba_requested_path
    set --local shuba_parent (realpath --canonicalize-existing -- (path dirname -- $shuba_requested_path)); or begin
        shuba_exact_source_fail "source output parent is unavailable: $shuba_requested_path"
        return 1
    end
    set --local shuba_name (path basename -- $shuba_requested_path)
    shuba_exact_source_validate_component $shuba_name; or return 1
    printf '%s\n' $shuba_parent/$shuba_name
end

function shuba_exact_source_build --argument-names shuba_project_root shuba_tag shuba_requested_output
    set --local shuba_output (shuba_exact_source_output_destination $shuba_requested_output); or return 1
    set --local shuba_work (mktemp --directory /tmp/shuba-exact-source-build.XXXXXX); or return 1
    shuba_exact_source_prepare_tree $shuba_project_root $shuba_tag $shuba_work; or begin
        rm -rf -- $shuba_work
        return 1
    end
    set --local shuba_archive_name $shuba_exact_source_stem_name.tar.zst
    set --local shuba_inventory_name $shuba_exact_source_stem_name.inventory.txt
    set --local shuba_manifest_name $shuba_exact_source_stem_name.submodules.txt
    shuba_exact_source_write_tree_inventory $shuba_work/$shuba_inventory_name $shuba_work $shuba_exact_source_top_name; or begin
        rm -rf -- $shuba_work
        return 1
    end
    shuba_exact_source_write_submodule_manifest $shuba_work/$shuba_manifest_name $shuba_exact_source_rows_path; or begin
        rm -rf -- $shuba_work
        return 1
    end
    shuba_exact_source_create_archive $shuba_work/$shuba_archive_name $shuba_work; or begin
        rm -rf -- $shuba_work
        return 1
    end
    shuba_exact_source_write_checksum $shuba_work/$shuba_archive_name.sha256 \
        $shuba_work/$shuba_archive_name $shuba_archive_name; or begin
        rm -rf -- $shuba_work
        return 1
    end
    for shuba_name in (shuba_exact_source_expected_names $shuba_exact_source_stem_name)
        chmod 0644 -- $shuba_work/$shuba_name; or begin
            rm -rf -- $shuba_work
            return 1
        end
    end
    shuba_atomic_publication_initialize (path dirname -- $shuba_output) $shuba_output 'exact source'; or begin
        rm -rf -- $shuba_work
        return 1
    end
    set --local shuba_stage (shuba_atomic_publication_stage_path); or begin
        rm -rf -- $shuba_work
        return 1
    end
    for shuba_name in (shuba_exact_source_expected_names $shuba_exact_source_stem_name)
        cp -- $shuba_work/$shuba_name $shuba_stage/$shuba_name; or begin
            shuba_atomic_publication_cleanup 1
            rm -rf -- $shuba_work
            return 1
        end
    end
    shuba_atomic_publication_publish; or begin
        shuba_atomic_publication_cleanup 1
        rm -rf -- $shuba_work
        return 1
    end
    shuba_exact_source_verify $shuba_project_root $shuba_tag $shuba_output false
    set --local shuba_verify_status $status
    if test $shuba_verify_status -ne 0
        shuba_atomic_publication_cleanup $shuba_verify_status
        rm -rf -- $shuba_work
        return $shuba_verify_status
    end
    shuba_atomic_publication_commit; or begin
        shuba_atomic_publication_cleanup 1
        rm -rf -- $shuba_work
        return 1
    end
    rm -rf -- $shuba_work
end
