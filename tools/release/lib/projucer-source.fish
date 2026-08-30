function shuba_projucer_validate_source --argument-names shuba_project_root shuba_source_root shuba_expected_commit
    if test -z "$shuba_expected_commit"
        set shuba_expected_commit e18f7f506c0b96f2c738a0bcd7fe6467a5005ad8
    end
    set --local shuba_submodule_line ($shuba_git_path -C $shuba_project_root submodule status -- third_party/JUCE); or return 1
    if not string match --regex --quiet "^ $shuba_expected_commit third_party/JUCE([ (]|\$)" -- $shuba_submodule_line
        shuba_fail 'JUCE is absent, conflicted, dirty at the gitlink level, or not at the required commit'
        return 1
    end
    test ($shuba_git_path -C $shuba_source_root rev-parse HEAD) = $shuba_expected_commit; or begin
        shuba_fail 'JUCE HEAD differs from the required release commit'
        return 1
    end
end
