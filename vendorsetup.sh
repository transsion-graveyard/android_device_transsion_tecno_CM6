#!/bin/bash
#
# CM6 compatibility patches for AOSP sources.
#
# vs_state=apply  (default) apply patches from this device tree's patches/
# vs_state=skip            remove only the patches applied by this script
#
# Usage from the AOSP source root:
#
#   source build/envsetup.sh
#   vs_state=skip source build/envsetup.sh
#
# Patch layout:
#
#   patches/<repo-relative-path>/<NNNN-description>.patch
#
# The target repository is <top>/<repo-relative-path>. For example:
#
#   patches/frameworks/base/0001-example.patch
#
# applies to:
#
#   <top>/frameworks/base
#
# Patch files are applied in LC_ALL=C sorted path order.
#
# Existing Git repositories are preserved. When a target has no Git metadata,
# this script creates a temporary baseline repository before applying patches.
# On vs_state=skip that target is reset to its baseline, cleaned, and its
# temporary Git metadata is removed.
#
# vs_state=apply requires a clean source for every target that has no recorded
# applied state. An already-applied, unrecorded patch is a hard error; it is
# never adopted automatically.
#
# Applied patches for existing Git repositories are recorded in:
#
#   <git-dir>/cm6-vendorsetup-applied
#
# so vs_state=skip never reverses a patch that this script did not apply.
#
# This script does not build anything. After vs_state=skip succeeds, run the
# normal build command; the source tree is back to its pre-patch state.
#
# Device-tree fixes must not be repeated here.
#

# ---------------------------------------------------------------------------
# Paths and configuration
# ---------------------------------------------------------------------------

_device_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
_top="$(cd "$_device_dir/../../../.." && pwd)"
_patches_dir="$_device_dir/patches"

_vs_state="${vs_state:-apply}"

_owned_marker="cm6-vendorsetup-owned"
_applied_state="cm6-vendorsetup-applied"

_failed=0
_patch_files=()

# ---------------------------------------------------------------------------
# Logging
# ---------------------------------------------------------------------------

_log() {
    printf '[CM6 vendorsetup] %s\n' "$*"
}

_error() {
    printf '[CM6 vendorsetup] ERROR: %s\n' "$*" >&2
    _failed=1
}

_fatal() {
    printf '[CM6 vendorsetup] FATAL: %s\n' "$*" >&2
}

# ---------------------------------------------------------------------------
# Git helpers
# ---------------------------------------------------------------------------

_has_local_git() {
    [[ -e "$1/.git" ]]
}

_git_dir() {
    git -C "$1" rev-parse --absolute-git-dir 2>/dev/null
}

_is_valid_local_git() {
    local target="$1"
    local root

    _has_local_git "$target" || return 1
    root="$(git -C "$target" rev-parse --show-toplevel 2>/dev/null)" || return 1
    [[ "$(cd "$root" && pwd -P)" == "$(cd "$target" && pwd -P)" ]]
}

_is_owned_git() {
    local target="$1"
    local git_dir

    # Only a real .git directory can be ours. A .git file/symlink belongs to
    # an existing repository and must never be removed by this script.
    [[ -d "$target/.git" && ! -L "$target/.git" ]] || return 1
    _is_valid_local_git "$target" || return 1
    git_dir="$(_git_dir "$target")" || return 1
    [[ -f "$git_dir/$_owned_marker" ]]
}

# ---------------------------------------------------------------------------
# Temporary Git baseline for non-Git targets
# ---------------------------------------------------------------------------

_init_tracking() {
    local target="$1"
    local baseline
    local git_dir

    _log "initializing temporary Git baseline: ${target#"$_top"/}"

    if ! git -C "$target" init --quiet; then
        _error "failed to initialize Git: $target"
        return 1
    fi

    # -f includes ignored files so the baseline can restore the exact source
    # state, including generated/ignored files that were present at init time.
    if ! git -C "$target" add -f -A; then
        _error "failed to stage original source: $target"
        rm -rf -- "$target/.git"
        return 1
    fi

    if ! git -C "$target" \
        -c user.name="CM6 Compatibility Patches" \
        -c user.email="cm6-vendorsetup@localhost" \
        commit --quiet --allow-empty \
        -m "CM6: save original AOSP source state"; then
        _error "failed to create baseline commit: $target"
        rm -rf -- "$target/.git"
        return 1
    fi

    baseline="$(git -C "$target" rev-parse HEAD 2>/dev/null)" || {
        _error "failed to identify baseline commit: $target"
        rm -rf -- "$target/.git"
        return 1
    }

    git_dir="$(_git_dir "$target")" || {
        _error "failed to locate Git directory: $target"
        rm -rf -- "$target/.git"
        return 1
    }

    if ! printf '%s\n' "$baseline" > "$git_dir/$_owned_marker"; then
        _error "failed to create Git ownership marker: $target"
        rm -rf -- "$target/.git"
        return 1
    fi

    _log "baseline saved: ${target#"$_top"/}"
    return 0
}

# ---------------------------------------------------------------------------
# Patch discovery
# ---------------------------------------------------------------------------

_discover_patches() {
    local allow_empty="${1:-0}"
    local patch
    local rel
    local target
    local target_rel

    _patch_files=()

    if [[ ! -d "$_patches_dir" ]]; then
        if [[ "$allow_empty" == "1" ]]; then
            _log "patches directory not found; nothing to discover"
            return 2
        fi
        _error "patches directory not found: $_patches_dir"
        return 1
    fi

    mapfile -t _patch_files < <(
        find "$_patches_dir" -type f -name '*.patch' -print | LC_ALL=C sort
    )

    if [[ "${#_patch_files[@]}" -eq 0 ]]; then
        if [[ "$allow_empty" == "1" ]]; then
            _log "no .patch files found; nothing to discover"
            return 2
        fi
        _error "no .patch files found in $_patches_dir"
        return 1
    fi

    for patch in "${_patch_files[@]}"; do
        rel="${patch#"$_patches_dir"/}"
        target_rel="$(dirname -- "$rel")"

        if [[ "$target_rel" == "." ]]; then
            _error "patch is not under a target directory: $rel"
            return 1
        fi

        target="$_top/$target_rel"
        if [[ ! -d "$target" ]]; then
            _error "missing patch target: $target (for $rel)"
            return 1
        fi
    done

    return 0
}

_target_rel() {
    local rel="${1#"$_patches_dir"/}"
    dirname -- "$rel"
}

_target_path() {
    printf '%s/%s\n' "$_top" "$(_target_rel "$1")"
}

# ---------------------------------------------------------------------------
# Applied-state helpers (existing Git repositories)
# ---------------------------------------------------------------------------

_sha256() {
    sha256sum "$1" | awk '{print $1}'
}

_state_file() {
    local target="$1"
    local git_dir

    _has_local_git "$target" || return 1
    git_dir="$(_git_dir "$target")" || return 1
    printf '%s/%s\n' "$git_dir" "$_applied_state"
}

_state_has() {
    local state="$1"
    local rel="$2"

    [[ -f "$state" ]] || return 1
    awk -F '\t' -v r="$rel" '$2 == r { found = 1 } END { exit !found }' "$state"
}

_state_drop() {
    local state="$1"
    local rel="$2"

    [[ -f "$state" ]] || return 0
    awk -F '\t' -v r="$rel" '$2 != r' "$state" > "$state.tmp" &&
        mv "$state.tmp" "$state"
}

_record_applied() {
    local target="$1"
    local patch="$2"
    local rel="$3"
    local state
    local hash

    state="$(_state_file "$target")" || {
        _error "cannot locate Git directory: $target"
        return 1
    }

    hash="$(_sha256 "$patch")" || {
        _error "cannot hash patch: $patch"
        return 1
    }

    if [[ -z "$hash" ]]; then
        _error "cannot hash patch: $patch"
        return 1
    fi

    # Drop a stale entry for the same patch before recording the fresh apply.
    _state_drop "$state" "$rel" || {
        _error "failed to update applied state: $state"
        return 1
    }

    if ! printf '%s\t%s\n' "$hash" "$rel" >> "$state"; then
        _error "failed to write applied state: $state"
        return 1
    fi

    return 0
}

# ---------------------------------------------------------------------------
# Apply preconditions
# ---------------------------------------------------------------------------

_check_apply_preconditions() {
    local -A targets=()
    local patch
    local rel
    local target
    local state
    local status

    for patch in "${_patch_files[@]}"; do
        targets["$(_target_path "$patch")"]=1
    done

    for target in "${!targets[@]}"; do
        if _has_local_git "$target"; then
            if ! _is_valid_local_git "$target"; then
                _error "invalid or unexpected Git repository: $target"
                return 1
            fi

            state="$(_state_file "$target")" || {
                _error "cannot locate Git directory: $target"
                return 1
            }

            for patch in "${_patch_files[@]}"; do
                [[ "$(_target_path "$patch")" == "$target" ]] || continue
                rel="${patch#"$_patches_dir"/}"

                if _state_has "$state" "$rel"; then
                    # Recorded patches are the only expected dirty state.
                    if ! git -C "$target" apply --check --reverse "$patch" >/dev/null 2>&1; then
                        _error "recorded patch is not applied cleanly: $rel"
                        _log "source is inconsistent; run vs_state=skip or inspect: $target"
                        return 1
                    fi
                    continue
                fi

                if git -C "$target" apply --check --reverse "$patch" >/dev/null 2>&1; then
                    _error "patch already applied but not recorded; source is not clean: $rel"
                    _log "revert the unrecorded patch or start from a clean source before vs_state=apply"
                    return 1
                fi

                if ! git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
                    _error "patch does not apply cleanly: $rel"
                    return 1
                fi
            done

            # No recorded state means this target must be a pristine source
            # tree. If state exists, its recorded patches are the only expected
            # modifications and were checked above.
            if [[ ! -s "$state" ]]; then
                status="$(git -C "$target" status --porcelain --untracked-files=all 2>/dev/null)" || {
                    _error "cannot inspect source tree: $target"
                    return 1
                }
                if [[ -n "$status" ]]; then
                    _error "source is not clean: $target"
                    _log "run 'git -C $target status' and commit, stash, or revert local changes before vs_state=apply"
                    return 1
                fi
            fi
        else
            # No Git metadata: the script will create a baseline from the
            # current directory. A patch that is already applied means the
            # source is not clean.
            for patch in "${_patch_files[@]}"; do
                [[ "$(_target_path "$patch")" == "$target" ]] || continue
                rel="${patch#"$_patches_dir"/}"

                if git -C "$target" apply --check --reverse "$patch" >/dev/null 2>&1; then
                    _error "patch already applied; source is not clean: $rel"
                    return 1
                fi

                if ! git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
                    _error "patch does not apply cleanly: $rel"
                    return 1
                fi
            done
        fi
    done

    return 0
}

# ---------------------------------------------------------------------------
# Apply
# ---------------------------------------------------------------------------

_apply_one() {
    local patch="$1"
    local rel
    local target
    local state

    rel="${patch#"$_patches_dir"/}"
    target="$(_target_path "$patch")"

    if ! _has_local_git "$target"; then
        if ! git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
            _error "patch does not apply cleanly: $rel"
            return 1
        fi
        _init_tracking "$target" || return 1
    else
        if ! _is_valid_local_git "$target"; then
            _error "invalid or unexpected Git repository: $target"
            return 1
        fi
    fi

    state="$(_state_file "$target")" || {
        _error "cannot locate Git directory: $target"
        return 1
    }

    # The applied-state file is authoritative for both existing and temporary
    # repositories. This avoids reverse-checking an earlier patch in a stack:
    # after patch 0002 is applied, 0001 alone no longer reverse-applies.
    if _state_has "$state" "$rel"; then
        _log "already applied (recorded): $rel"
        return 0
    fi

    if _is_owned_git "$target"; then
        # A temporary baseline is created from a clean source before apply.
        # It must never start from an already-patched tree.
        if git -C "$target" apply --check --reverse "$patch" >/dev/null 2>&1; then
            _error "patch already applied; source is not clean: $rel"
            return 1
        fi
        if ! git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
            _error "patch does not apply cleanly: $rel"
            return 1
        fi
    elif git -C "$target" apply --check --reverse "$patch" >/dev/null 2>&1; then
        _error "patch already applied but not recorded; source is not clean: $rel"
        _log "revert the unrecorded patch or start from a clean source before vs_state=apply"
        return 1
    fi

    if ! git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
        _error "patch does not apply cleanly: $rel"
        return 1
    fi

    if ! git -C "$target" apply "$patch"; then
        _error "failed to apply: $rel"
        return 1
    fi

    if ! _record_applied "$target" "$patch" "$rel"; then
        # Do not leave an unrecorded change behind.
        git -C "$target" apply --reverse "$patch" >/dev/null 2>&1 || true
        _error "failed to record applied patch; reverted: $rel"
        return 1
    fi

    _log "applied and recorded: $rel"
    return 0
}

_apply_patches() {
    local patch

    _discover_patches || return 1
    _check_apply_preconditions || return 1

    for patch in "${_patch_files[@]}"; do
        _apply_one "$patch" || return 1
    done

    _log "all patches applied"
    return 0
}

# ---------------------------------------------------------------------------
# Revert: existing Git repository
# ---------------------------------------------------------------------------

_reverse_recorded() {
    local target="$1"
    local state
    local line
    local hash
    local rel
    local patch
    local current

    state="$(_state_file "$target")" || {
        _error "cannot locate Git directory: $target"
        return 1
    }

    if [[ ! -f "$state" ]]; then
        _log "no DT patches recorded: ${target#"$_top"/}"
        return 0
    fi

    while [[ -s "$state" ]]; do
        line="$(tail -n 1 "$state")"

        if [[ -z "$line" || "$line" != *$'\t'* ]]; then
            _error "invalid applied-state entry in: $state"
            return 1
        fi

        hash="${line%%$'\t'*}"
        rel="${line#*$'\t'}"
        patch="$_patches_dir/$rel"

        if [[ ! -f "$patch" ]]; then
            _error "recorded patch is missing: $patch"
            return 1
        fi

        current="$(_sha256 "$patch")" || {
            _error "cannot hash patch: $patch"
            return 1
        }

        if [[ "$current" != "$hash" ]]; then
            _error "patch changed since apply; refusing to reverse: $rel"
            return 1
        fi

        if git -C "$target" apply --check --reverse "$patch" >/dev/null 2>&1; then
            if ! git -C "$target" apply --reverse "$patch"; then
                _error "failed to revert: $rel"
                return 1
            fi
            _log "reverted: $rel"
        elif git -C "$target" apply --check "$patch" >/dev/null 2>&1; then
            _log "already not applied; dropping stale state: $rel"
        else
            _error "cannot determine patch state: $rel"
            return 1
        fi

        if ! sed '$d' "$state" > "$state.tmp" ||
            ! mv "$state.tmp" "$state"; then
            _error "failed to update applied state: $state"
            return 1
        fi
    done

    rm -f "$state"
    return 0
}

# ---------------------------------------------------------------------------
# Revert: temporary Git baseline
# ---------------------------------------------------------------------------

_revert_temp() {
    local target="$1"
    local git_dir
    local baseline
    local status
    local head

    git_dir="$(_git_dir "$target")" || {
        _error "cannot locate Git directory: $target"
        return 1
    }

    baseline="$(cat "$git_dir/$_owned_marker")" || {
        _error "cannot read baseline: $target"
        return 1
    }

    if [[ -z "$baseline" ]]; then
        _error "empty baseline: $target"
        return 1
    fi

    _log "resetting temporary repo to baseline: ${target#"$_top"/}"

    if ! git -C "$target" reset --hard "$baseline"; then
        _error "failed to reset to baseline: $target"
        return 1
    fi

    # Remove files created after the baseline, including generated/ignored
    # files and nested untracked repositories.
    if ! git -C "$target" clean -ffdx; then
        _error "failed to clean temporary repo: $target"
        return 1
    fi

    status="$(git -C "$target" status --porcelain --untracked-files=all 2>/dev/null)" || {
        _error "cannot inspect temporary repo: $target"
        return 1
    }

    if [[ -n "$status" ]]; then
        _error "working tree differs from baseline: $target"
        return 1
    fi

    head="$(git -C "$target" rev-parse HEAD 2>/dev/null)" || {
        _error "cannot identify baseline HEAD: $target"
        return 1
    }

    if [[ "$head" != "$baseline" ]]; then
        _error "HEAD differs from baseline: $target"
        return 1
    fi

    if [[ -L "$target/.git" ]]; then
        _error "refusing to remove a symlinked .git: $target"
        return 1
    fi

    if ! rm -rf -- "$target/.git"; then
        _error "failed to remove temporary Git metadata: $target"
        return 1
    fi

    _log "removed temporary Git tracking: ${target#"$_top"/}"
    return 0
}

# ---------------------------------------------------------------------------
# Revert entry point
# ---------------------------------------------------------------------------

_skip_patches() {
    local -A targets=()
    local patch
    local target
    local discover_rc

    _discover_patches 1
    discover_rc=$?

    if [[ "$discover_rc" -eq 2 ]]; then
        _log "no device-tree patches to revert"
        return 0
    fi
    if [[ "$discover_rc" -ne 0 ]]; then
        return 1
    fi

    for patch in "${_patch_files[@]}"; do
        target="$(_target_path "$patch")"
        targets["$target"]=1
    done

    for target in "${!targets[@]}"; do
        if _is_owned_git "$target"; then
            _revert_temp "$target" || return 1
        elif _has_local_git "$target"; then
            _reverse_recorded "$target" || return 1
        else
            _log "not a Git repo and no temporary baseline; nothing to revert: ${target#"$_top"/}"
        fi
    done

    _log "selected patches reverted; source is back to the pre-patch state"
    return 0
}

# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

case "$_vs_state" in
    apply)
        _log "vs_state=apply: applying device-tree patches"
        _apply_patches
        ;;
    skip)
        _log "vs_state=skip: reverting device-tree patches"
        _skip_patches
        ;;
    *)
        _error "invalid vs_state '$_vs_state' (expected apply or skip)"
        ;;
esac

if [[ "$_failed" -ne 0 ]]; then
    _fatal "CM6 vendorsetup failed; fix the error before building."
    if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
        return 1
    else
        exit 1
    fi
fi

if [[ "${BASH_SOURCE[0]}" != "${0}" ]]; then
    return 0
else
    exit 0
fi
