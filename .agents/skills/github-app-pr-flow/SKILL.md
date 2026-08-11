---
name: github-app-pr-flow
description: Publish GitHub implementation work as YK's coding bot and PR reviews as YK's reviewer bot. Use for creating or updating Issues, branches, commits, pushes, PRs, PR comments, review comments, approvals, or change requests in this repository when agent work must not appear as the project owner's personal GitHub account.
---

# GitHub App PR Flow

Use `scripts/Invoke-GitHubApp.ps1` for every GitHub write made by an agent. Never expose, print, commit, or paste a private key or installation token. Do not change the user's global `gh` authentication.

## Credential opacity boundary

- Absolutely prohibit every agent and subagent from reading, querying, expanding, printing, logging, or inspecting `YK_CODING_PRIVATE_KEY_PATH`, `YK_REVIEWER_PRIVATE_KEY_PATH`, or any related credential environment variable to obtain a private-key location or content.
- Never open, read, copy, hash, summarize, upload, or inspect a private-key file, even for validation or troubleshooting.
- Invoke the wrapper with only `-Role coding` or `-Role reviewer`. Treat credential environment variables, private-key paths, private-key bytes, JWTs, and installation tokens as opaque to the agent.
- Allow only the trusted wrapper process to consume the configured environment variables and private-key bytes internally. Do not reproduce the wrapper's credential-loading logic in shell commands, scripts, tests, or debugging output.

## Choose the identity

- Use `coding` for implementation-side writes: Issue creation or updates, branch pushes, PR creation or updates, and replies that report fixes or test evidence.
- Use `reviewer` for review-side writes: review summaries, inline review comments, change requests, approvals, and re-review conclusions.
- Treat the primary/root agent as the reviewer and every implementation subagent as a coding agent.
- Keep read-only GitHub inspection on the most suitable authenticated surface. Identity separation is mandatory for writes.
- Do not let the reviewer bot push fixes to the PR it reviews. Do not let the coding bot approve its own work.

## Configure local secrets

The trusted wrapper obtains its opaque credential configuration from these environment variables; agents must know only their names and must never read their values:

- `YK_CODING_PRIVATE_KEY_PATH` for App ID `4559002`.
- `YK_REVIEWER_PRIVATE_KEY_PATH` for App ID `4559033`.

Optional overrides are `YK_CODING_APP_ID`, `YK_REVIEWER_APP_ID`, `YK_CODING_INSTALLATION_ID`, and `YK_REVIEWER_INSTALLATION_ID`. The script normally discovers the installation for the repository owner automatically.

Required GitHub App repository permissions:

- Coding bot: Metadata read, Contents write, Pull requests write, and Issues write.
- Reviewer bot: Metadata read, Contents read, Pull requests write, and Issues write.

Install both Apps on `Yuke-hd/mazda-can-telemetry`. Keep private keys outside the repository. If authentication fails, report the wrapper's sanitized error without inspecting environment values or private-key files.

## Run commands

From the repository root, first verify each identity without writing to GitHub:

```powershell
pwsh -File .agents/skills/github-app-pr-flow/scripts/Invoke-GitHubApp.ps1 -Role coding -Tool check
pwsh -File .agents/skills/github-app-pr-flow/scripts/Invoke-GitHubApp.ps1 -Role reviewer -Tool check
```

Run `gh` through the selected installation token:

```powershell
pwsh -File .agents/skills/github-app-pr-flow/scripts/Invoke-GitHubApp.ps1 -Role coding -Tool gh pr create --title "..." --body-file pr-body.md
pwsh -File .agents/skills/github-app-pr-flow/scripts/Invoke-GitHubApp.ps1 -Role reviewer -Tool gh api --method POST repos/Yuke-hd/mazda-can-telemetry/pulls/22/reviews --input review.json
```

Push an implementation branch through the coding App:

```powershell
pwsh -File .agents/skills/github-app-pr-flow/scripts/Invoke-GitHubApp.ps1 -Role coding -Tool git push origin HEAD
```

The wrapper keeps the token in process environment only and restores the caller's environment after the command. Never run its internals manually to obtain a token.

## Review protocol

1. Inspect the PR, diff, CI, and applicable `AGENTS.md` rules before writing.
2. Post only actionable findings. Use a formal PR review for approval or changes requested; use issue comments only for workflow notes that are not a review decision.
3. Have coding subagents post corrective commits and evidence through `-Role coding`.
4. Re-review the new head through `-Role reviewer`.
5. Leave merge and branch deletion to an explicit user request. Never merge merely because the reviewer bot approved.
