#!/usr/bin/fish --no-config

function shuba_ci_lint_fail
    printf 'CI lint: %s\n' (string join ' ' -- $argv) >&2
    return 1
end

function shuba_ci_lint_main
    if test (count $argv) -ne 0
        shuba_ci_lint_fail 'lint-ci.fish accepts no arguments'
        return 1
    end
    set --local shuba_root (realpath --canonicalize-existing -- (status dirname)/../..); or return 1
    set --local shuba_workflows $shuba_root/.github/workflows
    set --local shuba_uses (grep --recursive --extended-regexp \
        '^[[:space:]]*uses:[[:space:]]*[^[:space:]]+' $shuba_workflows); or return 1
    for shuba_use in $shuba_uses
        set --local shuba_reference (string replace --regex '^.*uses:[[:space:]]*' '' -- $shuba_use \
            | string replace --regex '[[:space:]]+#.*$' '' | string trim)
        if string match --quiet './*' -- $shuba_reference
            continue
        end
        if not string match --regex --quiet '^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+(/[A-Za-z0-9_.-]+)*@[0-9a-f]{40}$' -- $shuba_reference
            shuba_ci_lint_fail "workflow action is not pinned to a full commit: $shuba_reference"
            return 1
        end
    end
    set --local shuba_actionlint (command --search actionlint 2>/dev/null)
    if test $status -ne 0
        set --local shuba_actionlint_root $shuba_root/build/ci/actionlint-1.7.7
        set --local shuba_archive $shuba_actionlint_root/actionlint.tar.gz
        mkdir -p -- $shuba_actionlint_root; or return 1
        if not test -x $shuba_actionlint_root/actionlint
            curl --fail --location --proto '=https' --tlsv1.2 \
                --output $shuba_archive \
                https://github.com/rhysd/actionlint/releases/download/v1.7.7/actionlint_1.7.7_linux_amd64.tar.gz; or return 1
            echo '023070a287cd8cccd71515fedc843f1985bf96c436b7effaecce67290e7e0757  '$shuba_archive \
                | sha256sum --check --strict; or return 1
            tar --extract --gzip --file $shuba_archive --directory $shuba_actionlint_root actionlint; or return 1
            chmod 0755 $shuba_actionlint_root/actionlint; or return 1
            rm -f -- $shuba_archive
        end
        set shuba_actionlint $shuba_actionlint_root/actionlint
    end
    set --local shuba_shellcheck (command --search shellcheck); or begin
        shuba_ci_lint_fail 'shellcheck is required'
        return 1
    end
    $shuba_actionlint -shellcheck $shuba_shellcheck $shuba_workflows/*.yml; or return 1
    jq --exit-status '.version >= 6 and (.configurePresets | length) == 4' \
        $shuba_root/CMakePresets.json >/dev/null; or return 1
    set --local shuba_generated_header_includes (grep --recursive --line-number \
        --include='*.cpp' --include='*.hpp' --extended-regexp \
        '#include[[:space:]]*[<"]JuceHeader[.]h[>"]' \
        $shuba_root/Source/UI $shuba_root/tests 2>/dev/null)
    set --local shuba_generated_header_status $status
    if test $shuba_generated_header_status -eq 0
        shuba_ci_lint_fail "tracked host/test source includes ignored generated JuceHeader.h: $shuba_generated_header_includes"
        return 1
    else if test $shuba_generated_header_status -gt 1
        shuba_ci_lint_fail 'could not inspect tracked host source for generated JuceHeader.h dependencies'
        return 1
    end
    set --local shuba_diff_paths \
        .github \
        CMakeLists.txt \
        CMakePresets.json \
        docs/ci.md \
        release/release.properties \
        tools/ci \
        tools/release
    git -C $shuba_root diff --check -- $shuba_diff_paths; or return 1
    set --local shuba_oversized (find -P $shuba_root/Source $shuba_root/tests \
        -type f \( -name '*.cpp' -o -name '*.hpp' \) -print0 \
        | xargs -0 --max-args=1 wc -l | awk '$1 > 2000 { print }')
    if test -n "$shuba_oversized"
        shuba_ci_lint_fail "source-size gate failed: $shuba_oversized"
        return 1
    end
    printf '%s\n' 'CI lint: immutable actions, workflows, presets, whitespace, and source-size gates passed.'
end

shuba_ci_lint_main $argv
exit $status
