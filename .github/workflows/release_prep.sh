#!/usr/bin/env bash

# SPDX-FileCopyrightText: Copyright (c) The helly25/carve authors (github.com/helly25/carve)
# SPDX-License-Identifier: Apache-2.0
#
# Licensed under the Apache License, Version 2.0 (the "License");
# you may not use this file except in compliance with the License.
# You may obtain a copy of the License at
#
#      http://www.apache.org/licenses/LICENSE-2.0
#
# Unless required by applicable law or agreed to in writing, software
# distributed under the License is distributed on an "AS IS" BASIS,
# WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
# See the License for the specific language governing permissions and
# limitations under the License.

# Invoked by the release workflow
# (https://github.com/bazel-contrib/.github/blob/master/.github/workflows/release_ruleset.yaml).
# It builds the source archive that consumers fetch and prints the GitHub release
# notes (stdout). Nothing here publishes; the workflow only runs it for a pushed
# numeric-semver tag.

set -euo pipefail

PACKAGE_NAME="carve"
BAZELMOD_NAME="helly25_carve"

# Automatic var from workflow integration.
TAG="${GITHUB_REF_NAME}"

function die() {
  echo "ERROR: ${*}" 1>&2
  exit 1
}

# Computed vars.
PREFIX="${PACKAGE_NAME}-${TAG}"
ARCHIVE="${PACKAGE_NAME}-${TAG}.tar.gz"
BAZELMOD_VERSION="$(sed -rne 's,.*version = "([0-9]+([.][0-9]+)+.*)".*,\1,p' <MODULE.bazel | head -n1)"
# carve uses Keep a Changelog: the first "## [x.y.z]" heading (after "## [Unreleased]").
CHANGELOG_VERSION="$(sed -rne 's,^## \[([0-9]+([.][0-9]+)+)\].*,\1,p' <CHANGELOG.md | head -n1)"

if [ "${BAZELMOD_VERSION}" != "${TAG}" ]; then
  die "Tag = '${TAG}' does not match version = '${BAZELMOD_VERSION}' in MODULE.bazel."
fi
if [ "${CHANGELOG_VERSION}" != "${TAG}" ]; then
  die "Tag = '${TAG}' does not match the latest release version = '${CHANGELOG_VERSION}' in CHANGELOG.md."
fi

# Replace the root BUILD.bazel with an empty one for the released module: carve's
# own root targets (//:refresh, //:carve, //:refresh_compile_commands) are for
# development, not for dependents.
{
  cat tools/header.txt
  echo ""
  echo "\"\"\"Empty root BUILD for @${BAZELMOD_NAME}.\"\"\""
} >BUILD.bazel

# Comment the dev-only include so the released module does not reference the
# bazelmod/ dev modules (hedron, dwyu); bazelmod/ itself is excluded below.
perl -pi -e 's,^include\("//bazelmod:dev\.MODULE\.bazel"\),# include("//bazelmod:dev.MODULE.bazel"),' MODULE.bazel
grep -qE '^# include\("//bazelmod:dev\.MODULE\.bazel"\)' MODULE.bazel ||
  die "Failed to comment the dev include in MODULE.bazel (did the line change?)."

# Exclude development-only paths from the archive.
EXCLUDES=(
  ".bcr"
  ".github"
  ".pre-commit-config.yaml"
  "bazelmod"
  "tools"
)
{
  for exclude in "${EXCLUDES[@]}"; do
    echo "${exclude} export-ignore"
    if [[ -d ${exclude} ]]; then
      echo "${exclude}/** export-ignore"
    fi
  done
} >>.gitattributes

# Build the archive from the patched/generated worktree, not the committed "${TAG}"
# tree: `git archive "${TAG}"` reads the commit and would drop the edits above.
# Stage the worktree into a THROWAWAY index so the real index/checkout is never
# touched; export-ignore still applies via the staged .gitattributes (+
# --worktree-attributes).
TMP_INDEX="$(mktemp -u)"
GIT_INDEX_FILE="${TMP_INDEX}" git read-tree HEAD
GIT_INDEX_FILE="${TMP_INDEX}" git add --all
ARCHIVE_TREE="$(GIT_INDEX_FILE="${TMP_INDEX}" git write-tree)"
rm -f "${TMP_INDEX}"
git archive --format=tar.gz --prefix="${PREFIX}/" -o "${ARCHIVE}" --add-virtual-file="${PREFIX}/VERSION:${TAG}" --worktree-attributes "${ARCHIVE_TREE}"

# Release notes (stdout).
echo "# Version ${TAG}"
echo "## [Changelog](https://github.com/helly25/${PACKAGE_NAME}/blob/${TAG}/CHANGELOG.md)"
echo ""

# Print the body of the "## [${TAG}]" section from the Keep a Changelog file.
awk -v tag="${TAG}" '
  $0 ~ ("^## \\[" tag "\\]") { grab = 1; next }
  grab && /^## / { exit }
  grab { print }
' <CHANGELOG.md

cat <<EOF

## For Bazel MODULE.bazel

\`\`\`bzl
bazel_dep(name = "${BAZELMOD_NAME}", version = "${TAG}")
\`\`\`

carve builds from source through your toolchain on first use; later builds hit
Bazel's cache. See the README for the \`carve_refresh\` / \`cc_carve_aspect\` rules.
EOF
