# Contributing

This project connects to a safety-related vehicle network. No convenience change may bypass the receive-only, fail-silent, or isolated-bench requirements documented in this repository.

Every repository artifact must be written in English. This includes source code, comments, documentation, tests, configuration text, commit messages, Issues, and PR titles and bodies. The project owner and agents may use Chinese in direct conversation, but Chinese text must not be committed to the repository unless an explicit future localization requirement defines an exception.

## License and data submissions

Project-authored source, documentation, tests, and tooling are licensed under
Apache-2.0 (`LICENSE`). Third-party material keeps its original license and
must be recorded in `THIRD_PARTY_NOTICES.md`. In particular, opendbc is an MIT-
licensed candidate signal source: record its exact commit, files, access date,
and required notice before copying or generating signal definitions. Generated
definitions inherit the applicable source attribution; they are not
automatically Apache-2.0.

Never commit or attach a raw vehicle capture, VIN, credential, precise location,
absolute timestamp, or non-anonymized trip data. Real captures are for private
analysis unless transformed into a reviewed anonymized fixture. A fixture must
use a synthetic name, relative/coarsened time, no location or routine, and only
the minimum reviewed frames. Unknown bytes must be treated as unsafe until
reviewed. Follow the full [license and vehicle-data policy](docs/policies/license-and-vehicle-data.md)
and complete its checklist before submitting a fixture.

Raw captures are never accepted in commits, Issue attachments, PR comments,
screenshots, releases, or public links. For every submitted reviewed
anonymized fixture, generated signal definition, or third-party-derived
artifact, include this authorization statement in the PR, or write `Not
applicable` when no such artifact is included:

> I confirm that I own or have permission to submit this artifact, that I have
> followed the license and attribution requirements, and that I have removed
> credentials, VIN/vehicle identifiers, precise location, absolute time, and
> non-anonymized trip data. I understand that this public repository may
> redistribute an accepted fixture under its recorded license and that the
> maintainers may reject or remove it if its provenance, privacy, or safety
> status cannot be verified.

This authorization does not permit vehicle-side CAN transmission, diagnostic
polling, or any action outside the receive-only safety boundary.

## Issue and PR References

Use GitHub's native references, such as `#123`, when linking Issues and PRs. An
MCAN number is not required; contributors may use a descriptive title and
branch name without creating or maintaining a separate ticket-key sequence.

## Workflow

1. Create a GitHub Issue for non-trivial work and record scope and acceptance criteria.
2. Create a short-lived branch from the latest `main` using a descriptive name.
3. Commit only work within that ticket and run the relevant tests.
4. Open a PR, complete the template, and link the native Issue reference when applicable.
5. Resolve all conversations, correct the title, and squash merge.
6. Delete the branch after merge.

Do not use a long-lived `develop` or `release` branch. Branch names follow these patterns:

```text
feat/<slug>
fix/<slug>
docs/<slug>
test/<slug>
chore/<slug>
spike/<slug>
```

Example: `feat/twai-listen-only-capture` for GitHub Issue `#12`.

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
- Include `Refs #123` for GitHub linking. Use `Closes #123` only when the PR fully completes the Issue.
- Clean up WIP commits before merge. The final squashed title must comply with this format.

Example:

```text
feat(can): add timestamped listen-only frame capture

Keep exporter output independent from Mazda signal decoding.

Refs #12
```

## Pull Requests

PR titles also use Conventional Commit format because the title becomes the commit title on `main` after squash merge.

Every PR body must state:

- what changed and why;
- the related native GitHub Issue reference when applicable, and explicitly excluded scope;
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
