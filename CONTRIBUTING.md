# Contributing

This project connects to a safety-related vehicle network. No convenience change may bypass the receive-only, fail-silent, or isolated-bench requirements in `AGENTS.md`.

Every repository artifact must be written in English. This includes source code, comments, documentation, tests, configuration text, commit messages, Issues, and PR titles and bodies. The project owner and agents may use Chinese in direct conversation, but Chinese text must not be committed to the repository unless an explicit future localization requirement defines an exception.

## Ticket Keys

GitHub always assigns a native numeric reference such as `#123`; it cannot be configured to replace that reference with a custom prefix. The project maps that number directly to `MCAN-123`:

- GitHub Issue `#123` has project ticket key `MCAN-123`.
- Immediately after creating an Issue, update its title to `[MCAN-123] <descriptive title>` using the number returned by GitHub.
- Use `MCAN-123` in planning prose and `mcan-123` in branch names.
- Retain `#123` in PR bodies and commit footers because GitHub automation recognizes the native reference.
- Never allocate a separate MCAN sequence. GitHub Issues and PRs share one repository number sequence, so gaps in MCAN ticket keys are normal.

## Workflow

1. Create a GitHub Issue for non-trivial work, record scope and acceptance criteria, and update its title with the assigned `MCAN-<number>` key.
2. Create a short-lived branch from the latest `main` using the lowercase ticket key.
3. Commit only work within that ticket and run the relevant tests.
4. Open a PR, complete the template, and link both the MCAN key and native Issue reference.
5. Resolve all conversations, correct the title, and squash merge.
6. Delete the branch after merge.

Do not use a long-lived `develop` or `release` branch. Branch names follow these patterns:

```text
feat/mcan-<number>-<slug>
fix/mcan-<number>-<slug>
docs/mcan-<number>-<slug>
test/mcan-<number>-<slug>
chore/mcan-<number>-<slug>
spike/mcan-<number>-<slug>
```

Example: `feat/mcan-12-twai-listen-only-capture` for ticket `MCAN-12` / GitHub Issue `#12`.

## Commit Messages

Use Conventional Commits:

```text
<type>(<scope>): <imperative summary>
```

Allowed `type` values:

- `feat`: a new user-visible capability.
- `fix`: a defect correction.
- `docs`: documentation-only changes.
- `test`: tests or fixtures.
- `refactor`: restructuring without a behavior change.
- `perf`: a performance improvement.
- `build`: build system or dependency changes.
- `ci`: continuous-integration changes.
- `chore`: other maintenance.
- `revert`: a reverted change.

Recommended scopes: `core`, `can`, `tcan485`, `capture`, `mazda-kf`, `simulator`, `argb`, `protocol`, `docs`, and `repo`.

Requirements:

- Write the summary as a lowercase English imperative without a trailing period and keep it within 72 characters where practical.
- Mark a breaking change with `type(scope)!:` and add a `BREAKING CHANGE:` body section.
- Explain why and describe safety or compatibility impact in the body; do not merely repeat the diff.
- Record the project key as `Ticket: MCAN-123` and retain `Refs #123` for GitHub linking. Use `Closes #123` only when the PR fully completes the Issue.
- Clean up WIP commits before merge. The final squashed title must comply with this format.

Example:

```text
feat(can): add timestamped listen-only frame capture

Keep exporter output independent from Mazda signal decoding.

Ticket: MCAN-12
Refs #12
```

## Pull Requests

PR titles also use Conventional Commit format because the title becomes the commit title on `main` after squash merge.

Every PR body must state:

- what changed and why;
- the related `MCAN-<number>` ticket, native GitHub Issue reference, and explicitly excluded scope;
- which automated, bench, and vehicle tests were and were not run;
- the impact on listen-only operation, CAN transmission paths, freshness/fail-silent behavior, and vehicle release artifacts;
- affected hardware revision, pins, bitrate, or candidate DBC provenance;
- risks, rollback, and follow-up work;
- whether the change contains sensitive vehicle data, credentials, or third-party material.

Active CAN transmission may exist only in a prominently marked isolated-bench target. Any change involving `BENCH_ACK_ONLY` or a simulator transmission path must identify itself as `bench-only` in the PR title or body and prove that vehicle builds remain unable to transmit data frames.

## Merge and Protection

- `main` accepts squash merges only.
- Force-push and deletion are prohibited.
- All review conversations must be resolved.
- Stable CI checks will become required status checks after CI is established.
- No approval is required while the project has one maintainer. Raise the requirement to at least one approval after another maintainer joins.
