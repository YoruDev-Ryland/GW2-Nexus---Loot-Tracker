#!/usr/bin/env bash
# release.sh — Tag a new release and push to GitHub so the Actions build fires.
#
# Usage:
#   ./release.sh           # prompts for version
#   ./release.sh v1.2.3    # uses the supplied version

set -euo pipefail

# ── Colour helpers ─────────────────────────────────────────────────────────────
RED='\033[0;31m'; GREEN='\033[0;32m'; YELLOW='\033[1;33m'
CYAN='\033[0;36m'; BOLD='\033[1m'; NC='\033[0m'

info()  { echo -e "${CYAN}▶${NC}  $*"; }
ok()    { echo -e "${GREEN}✔${NC}  $*"; }
warn()  { echo -e "${YELLOW}⚠${NC}  $*"; }
die()   { echo -e "${RED}✘${NC}  $*" >&2; exit 1; }

# ── Sanity checks ──────────────────────────────────────────────────────────────

command -v git >/dev/null 2>&1 || die "git is not installed."

# Must be run from inside the repo
git rev-parse --git-dir >/dev/null 2>&1 || die "Not inside a git repository."

# Fetch latest remote state so we can compare properly
info "Fetching remote state..."
git fetch --tags origin 2>/dev/null || warn "Could not fetch from origin (offline?)"

# Warn if not on main/master
BRANCH=$(git symbolic-ref --short HEAD 2>/dev/null || echo "detached")
if [[ "$BRANCH" != "main" && "$BRANCH" != "master" ]]; then
    warn "You are on branch '${BRANCH}', not 'main'. Continue? [y/N]"
    read -r CONFIRM
    [[ "$CONFIRM" =~ ^[Yy]$ ]] || die "Aborted."
fi

# Warn if there are uncommitted changes
if ! git diff --quiet || ! git diff --cached --quiet; then
    warn "You have uncommitted changes. They will NOT be included in this release."
    warn "Continue? [y/N]"
    read -r CONFIRM
    [[ "$CONFIRM" =~ ^[Yy]$ ]] || die "Aborted."
fi

# Check that origin is actually reachable
if ! git remote get-url origin >/dev/null 2>&1; then
    die "No 'origin' remote found. Add one with: git remote add origin <url>"
fi

REMOTE_URL=$(git remote get-url origin)

# ── Version input ──────────────────────────────────────────────────────────────

VERSION="${1:-}"

if [[ -z "$VERSION" ]]; then
    # Show the last few tags to help the user pick the next one
    LAST_TAG=$(git describe --tags --abbrev=0 2>/dev/null || echo "(none)")
    echo
    echo -e "${BOLD}Last release tag:${NC} ${LAST_TAG}"
    echo
    read -rp "New version tag (e.g. v1.2.3): " VERSION
fi

# Normalise: add 'v' prefix if missing
[[ "$VERSION" == v* ]] || VERSION="v${VERSION}"

# Validate semver pattern
if ! [[ "$VERSION" =~ ^v[0-9]+\.[0-9]+\.[0-9]+(-[a-zA-Z0-9._-]+)?(\+[a-zA-Z0-9._-]+)?$ ]]; then
    die "Invalid version '${VERSION}'. Expected format: v<major>.<minor>.<patch>"
fi

# Check the tag doesn't already exist locally or remotely
if git rev-parse "$VERSION" >/dev/null 2>&1; then
    die "Tag '${VERSION}' already exists locally."
fi
if git ls-remote --tags origin "refs/tags/${VERSION}" | grep -q .; then
    die "Tag '${VERSION}' already exists on origin."
fi

# ── Summary & confirmation ─────────────────────────────────────────────────────

echo
echo -e "${BOLD}Release summary${NC}"
echo -e "  Tag:     ${GREEN}${VERSION}${NC}"
echo -e "  Branch:  ${BRANCH}"
echo -e "  Remote:  ${REMOTE_URL}"
echo -e "  Commit:  $(git rev-parse --short HEAD)  $(git log -1 --pretty=%s)"
echo
read -rp "Create and push tag '${VERSION}'? [y/N] " CONFIRM
[[ "$CONFIRM" =~ ^[Yy]$ ]] || die "Aborted."

# ── Create & push annotated tag ────────────────────────────────────────────────

info "Creating annotated tag ${VERSION}..."
git tag -a "$VERSION" -m "Release ${VERSION}"
ok "Tag created."

info "Pushing tag to origin..."
git push origin "$VERSION"
ok "Tag pushed."

# ── Print follow-up info ───────────────────────────────────────────────────────

# Derive the GitHub URL from the remote (handles both HTTPS and SSH remotes)
GITHUB_URL=""
if [[ "$REMOTE_URL" =~ github\.com[:/](.+/.+?)(\.git)?$ ]]; then
    REPO_PATH="${BASH_REMATCH[1]}"
    GITHUB_URL="https://github.com/${REPO_PATH}"
fi

echo
ok "Done! GitHub Actions is now building your release."
if [[ -n "$GITHUB_URL" ]]; then
    echo
    echo -e "  Actions:  ${CYAN}${GITHUB_URL}/actions${NC}"
    echo -e "  Releases: ${CYAN}${GITHUB_URL}/releases/tag/${VERSION}${NC}"
fi
echo
echo -e "${YELLOW}The DLL will appear under Releases once the workflow completes (~5 min).${NC}"
