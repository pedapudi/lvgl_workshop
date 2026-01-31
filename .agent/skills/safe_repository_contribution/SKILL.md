---
name: Safe Repository Contribution Workflow
description: A workflow to safely contribute changes to a Git repository, ensuring no redundant work, conflicts, or external side effects.
---

# Safe Repository Contribution Workflow

This skill outlines the necessary steps to verify the state of a repository before creating branches, commits, or pull requests. It strictly mandates working within the active workspace to verify isolation.

## Core Rules

1.  **Workspace Isolation**: NEVER modify repositories outside the current active workspace (e.g., do not edit `~/git/other-repo` directly).
2.  **Clone Locally**: Always clone the target repository into the current workspace (e.g., `./.work/`) to perform your work.
3.  **Clean Up**: Remove the temporary clone after the PR is submitted.

## Steps

1.  **Locate Target Repository**
    First, check if the repository is already present in your current workspace (e.g., in `external/`, `components/`, or `deps/`).
    - If found, ensure it is a safe git repository (`git -C path/to/repo status`).
    - If **NOT** found, clone it into a temporary directory:
      ```bash
      mkdir -p .work
      gh repo clone <owner>/<repo> .work/<repo_name>
      cd .work/<repo_name>
      ```
    - If using an existing directory, `cd` into it.

2.  **Fetch System State**
    Update your local view of the remote repository.
    ```bash
    git fetch origin
    ```

3.  **Verify Existing Work**
    Check if the file, feature, or change you plan to implement already exists in the remote `main` branch.
    - Check for files:
      ```bash
      git ls-tree -r origin/main | grep <filename_or_directory>
      ```
    - Check for commits:
      ```bash
      git log origin/main --grep="<feature_keywords>"
      ```

4.  **Branch Creation**
    Create a new feature branch from the latest `origin/main`.
    ```bash
    git checkout -b feature/my-feature-name origin/main
    ```

5.  **Implementation & Verification**
    - Implement your changes within this cloned directory.
    - Verify they work locally.
    - Check the diff to ensure *only* your changes are included.
      ```bash
      git diff origin/main --stat
      ```

6.  **Pull Request**
    - Push the branch.
    - Create the PR.
    - **CRITICAL**: Before creating the PR, check for existing open PRs.
      ```bash
      gh pr list --search "<keywords>"
      gh pr create --base main --head feature/my-feature-name --title "..." --body "..."
      ```

7.  **Clean Up**
    Once the PR is created, you can safely remove the temporary directory.
    ```bash
    cd ../..
    rm -rf .work
    ```

## Error Recovery

If you discover you have created a redundant PR or branch:
1.  **Close the PR**: `gh pr close <pr_number>`
2.  **Delete the Remote Branch**: `git push origin --delete <branch_name>`
