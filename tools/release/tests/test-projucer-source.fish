#!/usr/bin/fish --no-config

function shuba_projucer_source_test_fail
    printf 'Projucer source-authority tests: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_projucer_source_expect_rejection --argument-names shuba_description shuba_project_root shuba_source_root shuba_expected_commit shuba_expected_message
    set --local shuba_output \
        (shuba_projucer_validate_source $shuba_project_root $shuba_source_root $shuba_expected_commit 2>&1)
    set --local shuba_status $status
    if test $shuba_status -eq 0
        shuba_projucer_source_test_fail "accepted $shuba_description"
        return 1
    end
    if not string match --quiet "*$shuba_expected_message*" -- (string join \n -- $shuba_output)
        shuba_projucer_source_test_fail "reported the wrong failure for $shuba_description"
        return 1
    end
end

function shuba_projucer_source_write_commit --argument-names shuba_repository shuba_contents shuba_message
    printf '%s\n' $shuba_contents >$shuba_repository/source.txt; or return 1
    git -C $shuba_repository add source.txt; or return 1
    git -C $shuba_repository commit --quiet --message $shuba_message; or return 1
    git -C $shuba_repository rev-parse HEAD
end

function shuba_projucer_source_test_main
    set --global shuba_projucer_source_test_root \
        (mktemp --directory /tmp/shuba-projucer-source-tests.XXXXXX); or return 1
    set --local shuba_origin $shuba_projucer_source_test_root/juce-origin
    set --local shuba_root $shuba_projucer_source_test_root/root
    set --local shuba_source_root $shuba_root/third_party/JUCE
    mkdir -p $shuba_origin $shuba_root/third_party; or return 1
    git -C $shuba_origin init --quiet; or return 1
    git -C $shuba_origin config user.name 'Shuba Test'; or return 1
    git -C $shuba_origin config user.email 'shuba-test@example.invalid'; or return 1

    shuba_projucer_source_write_commit $shuba_origin original original >/dev/null; or return 1
    git -C $shuba_origin tag 9.0.1; or return 1
    set --local shuba_expected_commit \
        (shuba_projucer_source_write_commit $shuba_origin expected expected); or return 1
    set --local shuba_wrong_commit \
        (shuba_projucer_source_write_commit $shuba_origin wrong wrong); or return 1

    git -C $shuba_root init --quiet; or return 1
    git -C $shuba_root config user.name 'Shuba Test'; or return 1
    git -C $shuba_root config user.email 'shuba-test@example.invalid'; or return 1
    git -c protocol.file.allow=always -C $shuba_root submodule add --quiet $shuba_origin third_party/JUCE; or return 1
    git -C $shuba_source_root checkout --quiet $shuba_expected_commit; or return 1
    git -C $shuba_root add .gitmodules third_party/JUCE; or return 1
    git -C $shuba_root commit --quiet --message root; or return 1
    git -C $shuba_source_root tag --delete 9.0.1 >/dev/null; or return 1
    if git -C $shuba_source_root rev-parse --verify --quiet 'refs/tags/9.0.1' >/dev/null
        shuba_projucer_source_test_fail 'tagless fixture retained the descriptive release tag'
        return 1
    end

    set --global shuba_git_path (command --search git); or return 1
    shuba_projucer_validate_source $shuba_root $shuba_source_root $shuba_expected_commit; or return 1

    git -C $shuba_source_root checkout --quiet $shuba_wrong_commit; or return 1
    shuba_projucer_source_expect_rejection wrong-head-and-gitlink $shuba_root $shuba_source_root $shuba_expected_commit \
        'JUCE is absent, conflicted, dirty at the gitlink level, or not at the required commit'; or return 1

    git -C $shuba_root add third_party/JUCE; or return 1
    git -C $shuba_root commit --quiet --message wrong-gitlink; or return 1
    shuba_projucer_source_expect_rejection wrong-gitlink $shuba_root $shuba_source_root $shuba_expected_commit \
        'JUCE is absent, conflicted, dirty at the gitlink level, or not at the required commit'; or return 1

    git -C $shuba_source_root checkout --quiet $shuba_expected_commit; or return 1
    shuba_projucer_source_expect_rejection expected-head-wrong-gitlink $shuba_root $shuba_source_root $shuba_expected_commit \
        'JUCE is absent, conflicted, dirty at the gitlink level, or not at the required commit'; or return 1

    printf '%s\n' 'Projucer tagless exact-commit source-authority and mutation tests: passed'
end

set --global shuba_projucer_source_test_root ''
function shuba_projucer_source_test_cleanup --on-event fish_exit
    if test -n "$shuba_projucer_source_test_root"; and test -d $shuba_projucer_source_test_root
        rm -rf -- $shuba_projucer_source_test_root
    end
end

set --local shuba_script_directory (status dirname)
source $shuba_script_directory/../lib/core.fish
source $shuba_script_directory/../lib/projucer-source.fish

shuba_projucer_source_test_main
exit $status
