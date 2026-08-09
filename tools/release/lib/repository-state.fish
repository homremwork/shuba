function shuba_repository_state_digest --argument-names shuba_repository_directory
    if not test -d $shuba_repository_directory; or test -L $shuba_repository_directory
        shuba_fail "repository is unavailable or symbolic-linked: $shuba_repository_directory"
        return 1
    end
    set --local shuba_state_directory (mktemp --directory /tmp/shuba-repository-state.XXXXXX); or return 1
    set --local shuba_descriptor $shuba_state_directory/descriptor
    set --local shuba_paths $shuba_state_directory/untracked.paths
    printf 'tracked-diff\0' >$shuba_descriptor
    and git -C $shuba_repository_directory diff --binary HEAD -- >>$shuba_descriptor
    and printf '\0untracked-files\0' >>$shuba_descriptor
    and git -C $shuba_repository_directory ls-files --others --exclude-standard -z >$shuba_paths
    if test $status -ne 0
        rm -rf -- $shuba_state_directory
        shuba_fail "could not collect repository state: $shuba_repository_directory"
        return 1
    end

    while read --null shuba_untracked_path
        set --local shuba_entry_path $shuba_repository_directory/$shuba_untracked_path
        printf 'path\0%s\0' $shuba_untracked_path >>$shuba_descriptor
        printf 'mode\0%s\0' (stat --format %f -- $shuba_entry_path) >>$shuba_descriptor
        if test -L $shuba_entry_path
            printf 'symlink\0%s\0' (readlink -- $shuba_entry_path) >>$shuba_descriptor
        else if test -f $shuba_entry_path
            printf 'file-sha256\0%s\0' (shuba_sha256_file $shuba_entry_path) >>$shuba_descriptor
        else
            rm -rf -- $shuba_state_directory
            shuba_fail "repository contains an unsupported untracked entry: $shuba_untracked_path"
            return 1
        end
    end <$shuba_paths

    set --local shuba_digest (shuba_sha256_file $shuba_descriptor)
    set --local shuba_digest_status $status
    rm -rf -- $shuba_state_directory
    if test $shuba_digest_status -ne 0
        return 1
    end
    printf '%s\n' $shuba_digest
end

function shuba_write_regular_file_inventory --argument-names shuba_inventory_path shuba_inventory_root
    set --erase argv[1..2]
    if not test -d $shuba_inventory_root; or test -L $shuba_inventory_root
        shuba_fail "inventory root is unavailable or symbolic-linked: $shuba_inventory_root"
        return 1
    end
    set --local shuba_temporary_directory (mktemp --directory /tmp/shuba-file-inventory.XXXXXX); or return 1
    set --local shuba_paths $shuba_temporary_directory/paths
    set --local shuba_rows $shuba_temporary_directory/rows
    printf '' >$shuba_rows
    for shuba_relative_root in $argv
        set --local shuba_input_path $shuba_inventory_root/$shuba_relative_root
        if not test -d $shuba_input_path; or test -L $shuba_input_path
            rm -rf -- $shuba_temporary_directory
            shuba_fail "inventory input directory is unavailable or symbolic-linked: $shuba_relative_root"
            return 1
        end
        set --local shuba_link_path (find -P $shuba_input_path -type l -print -quit)
        if test $status -ne 0
            rm -rf -- $shuba_temporary_directory
            shuba_fail "could not inspect inventory input links: $shuba_relative_root"
            return 1
        end
        if test -n "$shuba_link_path"
            rm -rf -- $shuba_temporary_directory
            shuba_fail "file inventory rejects symbolic links: $shuba_link_path"
            return 1
        end
        find -P $shuba_input_path -type f -print0 | sort --zero-terminated >$shuba_paths
        set --local shuba_pipeline_statuses $pipestatus
        for shuba_status in $shuba_pipeline_statuses
            if test $shuba_status -ne 0
                rm -rf -- $shuba_temporary_directory
                shuba_fail "could not enumerate inventory input: $shuba_relative_root"
                return 1
            end
        end
        while read --null shuba_file_path
            set --local shuba_relative_path (string replace "$shuba_inventory_root/" '' -- $shuba_file_path | string collect)
            if string match --regex --quiet '[[:cntrl:]|]' -- $shuba_relative_path
                rm -rf -- $shuba_temporary_directory
                shuba_fail 'file inventory rejects control characters and separators in paths'
                return 1
            end
            printf 'file|%s|%s|%s\n' (stat --format %a -- $shuba_file_path) (shuba_sha256_file $shuba_file_path) $shuba_relative_path >>$shuba_rows
        end <$shuba_paths
    end
    begin
        printf 'file_inventory_schema_version=1\n'
        sort --unique -- $shuba_rows
    end >$shuba_inventory_path
    set --local shuba_inventory_status $status
    rm -rf -- $shuba_temporary_directory
    return $shuba_inventory_status
end

function shuba_validate_initialized_submodules --argument-names shuba_project_root
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    set --local shuba_inventory (mktemp /tmp/shuba-submodule-inventory.XXXXXX); or return 1
    $shuba_git_path -C $shuba_project_root submodule status --recursive >$shuba_inventory
    if test $status -ne 0
        rm -f -- $shuba_inventory
        shuba_fail 'could not read the recursive submodule inventory'
        return 1
    end
    set --local shuba_count 0
    while read --local shuba_line
        test -n "$shuba_line"; or continue
        set --local shuba_marker (string sub --start 1 --length 1 -- "$shuba_line")
        set --local shuba_record (string sub --start 2 -- "$shuba_line" | string trim --left)
        set --local shuba_fields (string split ' ' -- $shuba_record)
        if test (count $shuba_fields) -lt 2
            rm -f -- $shuba_inventory
            shuba_fail "could not parse recursive submodule record: $shuba_line"
            return 1
        end
        set --local shuba_commit $shuba_fields[1]
        set --local shuba_path $shuba_fields[2]
        if contains -- $shuba_marker - U
            rm -f -- $shuba_inventory
            shuba_fail "recursive submodule is absent or conflicted: $shuba_path"
            return 1
        end
        if not string match --regex --quiet '^[0-9a-f]{40,64}$' -- $shuba_commit
            rm -f -- $shuba_inventory
            shuba_fail "recursive submodule commit is malformed: $shuba_path"
            return 1
        end
        if not string match --regex --quiet '^third_party/[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*$' -- $shuba_path
            rm -f -- $shuba_inventory
            shuba_fail "recursive submodule path is unsafe or unsupported: $shuba_path"
            return 1
        end
        set --local shuba_repository $shuba_project_root/$shuba_path
        if not test -d $shuba_repository; or test -L $shuba_repository
            rm -f -- $shuba_inventory
            shuba_fail "recursive submodule directory is unavailable or symbolic-linked: $shuba_path"
            return 1
        end
        set --local shuba_head ($shuba_git_path -C $shuba_repository rev-parse HEAD); or begin
            rm -f -- $shuba_inventory
            return 1
        end
        if test "$shuba_head" != "$shuba_commit"
            rm -f -- $shuba_inventory
            shuba_fail "recursive submodule HEAD differs from its status record: $shuba_path"
            return 1
        end
        set shuba_count (math $shuba_count + 1)
    end <$shuba_inventory
    rm -f -- $shuba_inventory
    if test $shuba_count -eq 0
        shuba_fail 'the project has no initialized recursive submodules'
        return 1
    end
end

function shuba_write_build_input_manifest --argument-names shuba_manifest_path shuba_project_root
    set --local shuba_temporary_directory (mktemp --directory /tmp/shuba-build-inputs.XXXXXX); or return 1
    set --local shuba_paths $shuba_temporary_directory/paths
    set --local shuba_rows $shuba_temporary_directory/rows
    printf '' >$shuba_rows
    for shuba_relative_root in Source Localization Assets tests tools/release release
        set --local shuba_input_root $shuba_project_root/$shuba_relative_root
        if not test -d $shuba_input_root; or test -L $shuba_input_root
            rm -rf -- $shuba_temporary_directory
            shuba_fail "build-input directory is unavailable or symbolic-linked: $shuba_relative_root"
            return 1
        end
        set --local shuba_link (find -P $shuba_input_root -type l -print -quit)
        if test $status -ne 0; or test -n "$shuba_link"
            rm -rf -- $shuba_temporary_directory
            shuba_fail "build-input directory contains a symbolic link or could not be inspected: $shuba_relative_root"
            return 1
        end
        find -P $shuba_input_root -type f -print0 | sort --zero-terminated >$shuba_paths
        set --local shuba_find_statuses $pipestatus
        for shuba_status in $shuba_find_statuses
            if test $shuba_status -ne 0
                rm -rf -- $shuba_temporary_directory
                shuba_fail "could not enumerate build-input directory: $shuba_relative_root"
                return 1
            end
        end
        while read --null shuba_file_path
            set --local shuba_relative_path (string replace "$shuba_project_root/" '' -- $shuba_file_path | string collect)
            if string match --regex --quiet '[[:cntrl:]|]' -- $shuba_relative_path
                rm -rf -- $shuba_temporary_directory
                shuba_fail 'build-input manifest rejects control characters and separators in paths'
                return 1
            end
            printf 'file|%s|%s|%s\n' (stat --format %a -- $shuba_file_path) \
                (shuba_sha256_file $shuba_file_path) $shuba_relative_path >>$shuba_rows
        end <$shuba_paths
    end
    for shuba_relative_path in Shuba.jucer CMakeLists.txt
        set --local shuba_file_path $shuba_project_root/$shuba_relative_path
        shuba_require_regular_file $shuba_file_path; or begin
            rm -rf -- $shuba_temporary_directory
            return 1
        end
        printf 'file|%s|%s|%s\n' (stat --format %a -- $shuba_file_path) \
            (shuba_sha256_file $shuba_file_path) $shuba_relative_path >>$shuba_rows
    end
    if not test -s $shuba_rows
        rm -rf -- $shuba_temporary_directory
        shuba_fail 'build-input manifest is unexpectedly empty'
        return 1
    end
    set --local shuba_duplicate (sort $shuba_rows | uniq --repeated | head --lines 1)
    if test -n "$shuba_duplicate"
        rm -rf -- $shuba_temporary_directory
        shuba_fail 'build-input manifest contains a duplicate record'
        return 1
    end
    begin
        printf 'build_input_manifest_schema_version=1\n'
        sort -- $shuba_rows
    end >$shuba_manifest_path
    set --local shuba_manifest_status $status
    rm -rf -- $shuba_temporary_directory
    return $shuba_manifest_status
end

function shuba_write_submodule_state --argument-names shuba_output_path shuba_project_root
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    set --local shuba_temporary_directory (mktemp --directory /tmp/shuba-submodule-state.XXXXXX); or return 1
    set --local shuba_inventory $shuba_temporary_directory/inventory
    set --local shuba_rows $shuba_temporary_directory/rows
    printf '' >$shuba_rows
    $shuba_git_path -C $shuba_project_root submodule status --recursive >$shuba_inventory
    if test $status -ne 0
        rm -rf -- $shuba_temporary_directory
        shuba_fail 'could not read recursive submodule state'
        return 1
    end
    while read --local shuba_line
        test -n "$shuba_line"; or continue
        set --local shuba_record (string sub --start 2 -- "$shuba_line" | string trim --left)
        set --local shuba_fields (string split ' ' -- $shuba_record)
        if test (count $shuba_fields) -lt 2
            rm -rf -- $shuba_temporary_directory
            shuba_fail 'could not parse recursive submodule state'
            return 1
        end
        set --local shuba_path $shuba_fields[2]
        if not string match --regex --quiet '^third_party/[A-Za-z0-9._+-]+(/[A-Za-z0-9._+-]+)*$' -- $shuba_path
            rm -rf -- $shuba_temporary_directory
            shuba_fail "recursive submodule-state path is unsafe or unsupported: $shuba_path"
            return 1
        end
        set --local shuba_repository $shuba_project_root/$shuba_path
        set --local shuba_commit ($shuba_git_path -C $shuba_repository rev-parse HEAD); or begin
            rm -rf -- $shuba_temporary_directory
            return 1
        end
        set --local shuba_status ($shuba_git_path -C $shuba_repository status \
            --porcelain=v1 --untracked-files=all --ignore-submodules=none); or begin
            rm -rf -- $shuba_temporary_directory
            return 1
        end
        set --local shuba_dirty false
        test (count $shuba_status) -eq 0; or set shuba_dirty true
        printf 'submodule|%s|commit=%s|dirty=%s|state_sha256=%s\n' $shuba_path \
            $shuba_commit $shuba_dirty (shuba_repository_state_digest $shuba_repository) >>$shuba_rows
    end <$shuba_inventory
    if not test -s $shuba_rows
        rm -rf -- $shuba_temporary_directory
        shuba_fail 'recursive submodule-state descriptor is unexpectedly empty'
        return 1
    end
    begin
        printf 'submodule_state_schema_version=1\n'
        sort -- $shuba_rows
    end >$shuba_output_path
    set --local shuba_state_status $status
    rm -rf -- $shuba_temporary_directory
    return $shuba_state_status
end

function shuba_capture_release_state --argument-names shuba_state_directory shuba_project_root
    if test -e $shuba_state_directory
        shuba_fail "release-state destination already exists: $shuba_state_directory"
        return 1
    end
    mkdir -p -- $shuba_state_directory; or return 1
    shuba_write_build_input_manifest $shuba_state_directory/build-inputs.manifest $shuba_project_root; or return 1
    shuba_write_submodule_state $shuba_state_directory/submodules.state $shuba_project_root; or return 1
    set --local shuba_git_path (shuba_resolve_command git); or return 1
    set --local shuba_root_status ($shuba_git_path -C $shuba_project_root status \
        --porcelain=v1 --untracked-files=all --ignore-submodules=none); or return 1
    set --local shuba_root_dirty false
    test (count $shuba_root_status) -eq 0; or set shuba_root_dirty true
    begin
        printf 'release_state_schema_version=1\n'
        printf 'root_commit=%s\n' ($shuba_git_path -C $shuba_project_root rev-parse HEAD)
        printf 'root_dirty=%s\n' $shuba_root_dirty
        printf 'root_worktree_state_sha256=%s\n' (shuba_repository_state_digest $shuba_project_root)
        printf 'build_input_manifest_sha256=%s\n' (shuba_sha256_file $shuba_state_directory/build-inputs.manifest)
        printf 'submodule_state_sha256=%s\n' (shuba_sha256_file $shuba_state_directory/submodules.state)
    end >$shuba_state_directory/release.state
end

function shuba_assert_release_state_unchanged --argument-names shuba_initial_directory shuba_recheck_directory shuba_project_root shuba_scope
    shuba_capture_release_state $shuba_recheck_directory $shuba_project_root; or return 1
    for shuba_filename in build-inputs.manifest submodules.state
        cmp --silent $shuba_initial_directory/$shuba_filename $shuba_recheck_directory/$shuba_filename; or begin
            shuba_fail "release $shuba_filename changed during $shuba_scope"
            return 1
        end
    end
    if test "$shuba_scope" = construction
        cmp --silent $shuba_initial_directory/release.state $shuba_recheck_directory/release.state; or begin
            shuba_fail 'root repository state changed during candidate construction'
            return 1
        end
    end
end
