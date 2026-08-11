#!/usr/bin/env python3
"""Validate the cross-platform GitHub App wrapper permission guardrails.

The wrappers need live GitHub credentials for an end-to-end check, so this
validation keeps the security-sensitive policy testable without contacting
GitHub or handling a private key. It checks that both implementations request
the same role-specific permissions and reject unexpected write permissions.
"""

from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BASH = ROOT / ".agents/skills/github-app-pr-flow/scripts/invoke-github-app.sh"
POWERSHELL = ROOT / ".agents/skills/github-app-pr-flow/scripts/Invoke-GitHubApp.ps1"


def require(text: str, fragment: str, description: str) -> None:
    if fragment not in text:
        raise AssertionError(f"missing {description}: {fragment}")


def main() -> None:
    bash = BASH.read_text(encoding="utf-8")
    powershell = POWERSHELL.read_text(encoding="utf-8")

    # Both wrappers must ask GitHub for the least privilege needed by each
    # identity, rather than inheriting every permission from the installation.
    require(
        bash,
        "requested_permissions='{\"contents\":\"write\",\"pull_requests\":\"write\",\"issues\":\"write\",\"metadata\":\"read\"}'",
        "coding permission request in Bash wrapper",
    )
    require(
        bash,
        "requested_permissions='{\"contents\":\"read\",\"pull_requests\":\"write\",\"issues\":\"write\",\"metadata\":\"read\"}'",
        "reviewer permission request in Bash wrapper",
    )
    require(bash, "--argjson permissions \"$requested_permissions\"", "Bash permission request payload")
    require(
        powershell,
        '@{ contents = "write"; pull_requests = "write"; issues = "write"; metadata = "read" }',
        "coding permission request in PowerShell wrapper",
    )
    require(
        powershell,
        '@{ contents = "read"; pull_requests = "write"; issues = "write"; metadata = "read" }',
        "reviewer permission request in PowerShell wrapper",
    )
    require(powershell, "permissions = $requestedPermissions", "PowerShell permission request payload")

    # A server-side or installation-policy change must not silently give an
    # agent an additional write capability after token creation.
    require(bash, 'select(.value == "write")', "Bash unexpected-write scan")
    require(bash, "allowed_write_permissions=(contents pull_requests issues)", "Bash coding write allowlist")
    require(bash, "allowed_write_permissions=(pull_requests issues)", "Bash reviewer write allowlist")
    require(powershell, 'if ([string]$property.Value -eq "write" -and $property.Name -notin $allowedWritePermissions)', "PowerShell unexpected-write scan")
    require(powershell, '@("contents", "pull_requests", "issues")', "PowerShell coding write allowlist")
    require(powershell, '@("pull_requests", "issues")', "PowerShell reviewer write allowlist")

    print("GitHub App wrapper permission policies are present for Bash and PowerShell.")


if __name__ == "__main__":
    main()
