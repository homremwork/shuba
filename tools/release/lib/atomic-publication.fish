function shuba_atomic_publication_initialize --argument-names shuba_parent shuba_destination shuba_label
    if test -L $shuba_parent; or not test -d $shuba_parent
        shuba_fail "$shuba_label parent is unavailable or symbolic-linked: $shuba_parent"
        return 1
    end
    set --local shuba_physical_parent (realpath --canonicalize-existing -- $shuba_parent); or return 1
    set --local shuba_destination_parent (realpath --canonicalize-existing -- (path dirname $shuba_destination)); or return 1
    if test "$shuba_physical_parent" != "$shuba_destination_parent"
        shuba_fail "$shuba_label destination is outside its owned parent"
        return 1
    end
    if test -L $shuba_destination
        shuba_fail "$shuba_label destination must not be a symbolic link"
        return 1
    end
    set --global shuba_publication_parent $shuba_physical_parent
    set --global shuba_publication_destination $shuba_destination
    set --global shuba_publication_label $shuba_label
    set --global shuba_publication_stage (mktemp --directory $shuba_physical_parent/.shuba-stage.XXXXXX); or return 1
    set --global shuba_publication_backup ''
    set --global shuba_publication_had_previous false
    set --global shuba_publication_published false
end

function shuba_atomic_publication_stage_path
    if not set --query shuba_publication_stage; or test -z "$shuba_publication_stage"
        shuba_fail 'atomic publication has not been initialized'
        return 1
    end
    printf '%s\n' $shuba_publication_stage
end

function shuba_atomic_publication_publish
    if not test -d $shuba_publication_stage; or test -L $shuba_publication_stage
        shuba_fail "$shuba_publication_label staging directory is unavailable"
        return 1
    end
    if test -e $shuba_publication_destination
        if not test -d $shuba_publication_destination; or test -L $shuba_publication_destination
            shuba_fail "$shuba_publication_label existing destination is not a safe directory"
            return 1
        end
        set --global shuba_publication_backup (mktemp --directory $shuba_publication_parent/.shuba-backup.XXXXXX); or return 1
        mv -- $shuba_publication_destination $shuba_publication_backup/previous; or return 1
        set --global shuba_publication_had_previous true
    end
    mv -- $shuba_publication_stage $shuba_publication_destination; or begin
        if test $shuba_publication_had_previous = true; and test -d $shuba_publication_backup/previous
            mv -- $shuba_publication_backup/previous $shuba_publication_destination
        end
        return 1
    end
    set --global shuba_publication_stage ''
    set --global shuba_publication_published true
end

function shuba_atomic_publication_rollback
    if not set --query shuba_publication_published; or test $shuba_publication_published != true
        return 0
    end
    set --local shuba_failed_destination ''
    if test $shuba_publication_had_previous = true; and test -d $shuba_publication_backup/previous
        if test -e $shuba_publication_destination
            set shuba_failed_destination $shuba_publication_backup/failed
            mv -- $shuba_publication_destination $shuba_failed_destination; or return 1
        end
        mv -- $shuba_publication_backup/previous $shuba_publication_destination; or return 1
    else
        rm -rf -- $shuba_publication_destination; or return 1
    end
    set --global shuba_publication_published false
end

function shuba_atomic_publication_commit
    if set --query shuba_publication_backup; and test -n "$shuba_publication_backup"
        rm -rf -- $shuba_publication_backup; or return 1
    end
    set --global shuba_publication_backup ''
    set --global shuba_publication_published false
end

function shuba_atomic_publication_cleanup --argument-names shuba_status
    set --local shuba_cleanup_status 0
    if test "$shuba_status" -ne 0
        shuba_atomic_publication_rollback; or set shuba_cleanup_status $status
    end
    if set --query shuba_publication_stage; and test -n "$shuba_publication_stage"; and test -e $shuba_publication_stage
        rm -rf -- $shuba_publication_stage; or set shuba_cleanup_status $status
    end
    if set --query shuba_publication_backup; and test -n "$shuba_publication_backup"; and test -e $shuba_publication_backup
        rm -rf -- $shuba_publication_backup; or set shuba_cleanup_status $status
    end
    return $shuba_cleanup_status
end
