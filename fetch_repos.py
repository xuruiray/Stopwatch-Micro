#!/usr/bin/env python3
"""Fetch the source dependencies declared in repos.json."""

from __future__ import annotations

import json
import re
import subprocess
from pathlib import Path
from typing import Any


ROOT = Path(__file__).resolve().parent
COMMIT_PATTERN = re.compile(r"[0-9a-f]{40}")


def run_git(
    repo: Path, *args: str, check: bool = True, quiet: bool = False
) -> subprocess.CompletedProcess[str]:
    """Run a Git command in a dependency checkout."""
    return subprocess.run(
        ["git", "-C", str(repo), *args],
        check=check,
        text=True,
        stdout=subprocess.DEVNULL if quiet else None,
        stderr=subprocess.DEVNULL if quiet else None,
    )


def apply_patch(repo: Path, patch: Path) -> None:
    """Apply a patch once and fail when it matches neither state."""
    if run_git(repo, "apply", "--check", str(patch), check=False, quiet=True).returncode == 0:
        run_git(repo, "apply", str(patch))
        print(f"Applied {patch.relative_to(ROOT)} to {repo.relative_to(ROOT)}")
        return

    if run_git(
        repo, "apply", "--reverse", "--check", str(patch), check=False, quiet=True
    ).returncode == 0:
        print(f"Patch already applied: {patch.relative_to(ROOT)}")
        return

    raise RuntimeError(f"Patch does not match {repo}: {patch}")


def clone_or_update_repo(config: dict[str, Any]) -> None:
    repo = ROOT / config["path"]
    commit = config.get("commit", "")
    if COMMIT_PATTERN.fullmatch(commit) is None:
        raise ValueError(f"{config['path']} must declare an immutable 40-character commit SHA")

    if repo.exists():
        if not (repo / ".git").exists():
            raise RuntimeError(f"Existing dependency is not a Git checkout: {repo}")
        run_git(repo, "remote", "set-url", "origin", config["url"])
    else:
        repo.mkdir(parents=True)
        run_git(repo, "init", quiet=True)
        run_git(repo, "remote", "add", "origin", config["url"])

    commit_exists = (
        run_git(repo, "cat-file", "-e", f"{commit}^{{commit}}", check=False, quiet=True).returncode
        == 0
    )
    if not commit_exists:
        run_git(repo, "fetch", "--depth", "1", "origin", commit)

    run_git(repo, "checkout", "--detach", commit)

    if config.get("with_submodules", False):
        run_git(repo, "submodule", "update", "--init", "--recursive")

    if patch_path := config.get("patch"):
        apply_patch(repo, ROOT / patch_path)


def fetch_dependencies() -> None:
    with (ROOT / "repos.json").open(encoding="utf-8") as config_file:
        repositories = json.load(config_file)

    for repository in repositories:
        clone_or_update_repo(repository)


if __name__ == "__main__":
    fetch_dependencies()
