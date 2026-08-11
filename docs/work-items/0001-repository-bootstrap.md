# Work Item 0001 — Repository Bootstrap and Governance

> Status: complete
>
> Date: 2026-08-11
>
> Type: repository / governance
>
> Product code: outside this work item

## Objective

Before creating product-development tickets, establish an auditable GitHub repository that supports a single maintainer without deadlocking the workflow and can strengthen its quality gates over time. This is the project's first formal work item.

## Adopted Defaults

- GitHub repository: `Yuke-hd/mazda-can-telemetry`.
- Description: `Read-only Mazda CX-5 KF CAN telemetry decoder, ESP32 exporter, and isolated bench simulator.`
- Visibility: **public** by explicit project decision. Project-authored material is Apache-2.0; third-party attribution and vehicle-data anonymization are governed by [the confirmed MCAN-2 policy](../policies/license-and-vehicle-data.md).
- Default branch: `main`.
- Work tracking: GitHub Issues. Map native Issue `#123` directly to ticket key `MCAN-123` and update the title to `[MCAN-123] ...` after creation. Do not create product backlog Issues until this work item passes acceptance.
- Development model: trunk-based development without a long-lived `develop` branch.
- Merge method: squash merge only. The Conventional Commit PR title becomes the merged commit title.
- Repository language: English for every artifact, including source, comments, documentation, tests, configuration text, commits, Issues, and PRs. The owner-agent conversation may use Chinese.

## Deliverables

1. A local Git repository and initial commit on `main`.
2. A public GitHub repository configured as `origin`.
3. `main` protection and repository merge settings.
4. Branch, commit, and PR rules in `CONTRIBUTING.md`.
5. `.github/pull_request_template.md`.
6. The `MCAN-<GitHub Issue number>` ticket-key convention.
7. Project introduction, ignore rules, and this work-item record.

## `main` Protection

The initial bootstrap commit is the only direct commit permitted before protection exists. Configure the following immediately after the first push:

- Require changes to merge through a pull request.
- Keep required approving reviews at `0` so a sole maintainer is not unable to approve their own PR. Open a separate Issue and raise this to at least `1` after another maintainer joins.
- Require every review conversation to be resolved.
- Apply the rule to administrators.
- Require linear history.
- Prohibit force-push.
- Prohibit deletion of `main`.
- Do not require status checks until stable CI exists; add them through a separate Issue afterward.
- Do not require CODEOWNERS approval until maintainership is defined.

Also disable merge commits and rebase merges, retain squash merge only, and automatically delete merged head branches.

## Branch Strategy

`main` must remain buildable and replay-testable and must never produce a vehicle firmware artifact with active CAN transmission capability. Use short-lived branches for every non-trivial change:

```text
feat/mcan-<number>-<slug>
fix/mcan-<number>-<slug>
docs/mcan-<number>-<slug>
test/mcan-<number>-<slug>
chore/mcan-<number>-<slug>
spike/mcan-<number>-<slug>
```

GitHub's native Issue reference cannot be customized. Map it rather than creating another counter: Issue `#123` is ticket `MCAN-123`. Immediately rename its title to `[MCAN-123] <descriptive title>`, use `mcan-123` in branch names, and retain `#123` where GitHub automatic linking is required. Issues and PRs share GitHub's number sequence, so gaps are expected and valid.

Use `fix/` for urgent corrections rather than creating a long-lived hotfix branch. Even a small spelling correction should preferably use a PR. Any future exception must first change the governance document and protection rule; do not bypass the rule by verbal agreement.

## Commit and PR Format

Commit and PR titles follow:

```text
<type>(<scope>): <imperative summary>
```

See `CONTRIBUTING.md` for allowed types and scopes, breaking changes, Issue references, and PR body requirements. Because merges are squashed, the PR title is the primary semantic record on `main`; correct it before merge.

## Execution Checklist

- [x] Select repository name, visibility, and description.
- [x] Define branch, commit, PR, and protection rules.
- [x] Add the PR template, README, and ignore rules.
- [x] Initialize local Git with `main` as the default branch.
- [x] Inspect the bootstrap diff and create the initial commit.
- [x] Create the public GitHub repository and configure `origin`.
- [x] Push `main`.
- [x] Configure and read back repository merge settings.
- [x] Configure and read back `main` protection.
- [x] Mark this work item complete.

## Acceptance Criteria

- `git status` is clean and local `main` tracks the remote.
- GitHub uses `main` as its default branch and has Issues enabled.
- Every product Issue uses the `MCAN-<native Issue number>` key and `[MCAN-<number>]` title prefix without a separate sequence allocator.
- Direct force-push and deletion of `main` are prohibited, and ordinary changes require PRs.
- The repository permits squash merge only and automatically deletes merged branches.
- This work item matches settings read back from the GitHub API.
- No product code, Wi-Fi password, VIN, trip data, or other sensitive information is included.
- Every repository artifact is in English.
- Product milestones are converted into GitHub Issues only after this work item is complete.

## Deferred Items

- Required CI checks: wait for a stable CI workflow.
- At least one reviewer: wait for a second maintainer.
- CODEOWNERS: wait for clear maintenance ownership.
- Project license and vehicle-data policy: confirmed in MCAN-2; raw vehicle captures remain private and only reviewed anonymized fixtures may be published.
- Release and tag policy: define before the first runnable firmware.
