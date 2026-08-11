#!/usr/bin/env bash

set -euo pipefail

role=""
tool="check"
repository="Yuke-hd/mazda-can-telemetry"

usage() {
  printf '%s\n' \
    "Usage: $0 --role coding|reviewer [--tool check|gh|git]" \
    "          [--repository OWNER/REPO] [-- COMMAND ...]" >&2
  exit 2
}

while (($# > 0)); do
  case "$1" in
    --role)
      (($# >= 2)) || usage
      role="$2"
      shift 2
      ;;
    --tool)
      (($# >= 2)) || usage
      tool="$2"
      shift 2
      ;;
    --repository)
      (($# >= 2)) || usage
      repository="$2"
      shift 2
      ;;
    --)
      shift
      break
      ;;
    *)
      usage
      ;;
  esac
done

[[ "$role" == "coding" || "$role" == "reviewer" ]] || usage
[[ "$tool" == "check" || "$tool" == "gh" || "$tool" == "git" ]] || usage
[[ "$repository" =~ ^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$ ]] || usage

for dependency in curl jq openssl; do
  command -v "$dependency" >/dev/null 2>&1 || {
    printf "Required command '%s' was not found.\n" "$dependency" >&2
    exit 1
  }
done

if [[ "$tool" == "gh" ]]; then
  command -v gh >/dev/null 2>&1 || {
    printf "Required command 'gh' was not found.\n" >&2
    exit 1
  }
elif [[ "$tool" == "git" ]]; then
  command -v git >/dev/null 2>&1 || {
    printf "Required command 'git' was not found.\n" >&2
    exit 1
  }
fi

if [[ "$role" == "coding" ]]; then
  default_app_id="4559002"
  app_id_environment="YK_CODING_APP_ID"
  key_path_environment="YK_CODING_PRIVATE_KEY_PATH"
  installation_environment="YK_CODING_INSTALLATION_ID"
else
  default_app_id="4559033"
  app_id_environment="YK_REVIEWER_APP_ID"
  key_path_environment="YK_REVIEWER_PRIVATE_KEY_PATH"
  installation_environment="YK_REVIEWER_INSTALLATION_ID"
fi

app_id="${!app_id_environment:-$default_app_id}"
key_path="${!key_path_environment:-}"
installation_id="${!installation_environment:-}"

if [[ -z "$key_path" ]]; then
  printf "Set %s to the local private-key file for role '%s'.\n" "$key_path_environment" "$role" >&2
  exit 1
fi
if [[ ! -f "$key_path" ]]; then
  printf "Private key file not found. Check the configured private-key path for role '%s'.\n" "$role" >&2
  exit 1
fi

temporary_directory="$(mktemp -d)"
cleanup() {
  rm -f -- "$temporary_directory/signature.bin"
  rmdir -- "$temporary_directory" 2>/dev/null || true
}
trap cleanup EXIT

base64url() {
  openssl base64 -A | tr '+/' '-_' | tr -d '='
}

now="$(date +%s)"
header_part="$(printf '%s' '{"alg":"RS256","typ":"JWT"}' | base64url)"
payload_part="$(jq -cn --argjson iat "$((now - 60))" --argjson exp "$((now + 540))" --arg iss "$app_id" \
  '{iat: $iat, exp: $exp, iss: $iss}' | base64url)"
unsigned_token="$header_part.$payload_part"
printf '%s' "$unsigned_token" | openssl dgst -sha256 -sign "$key_path" -out "$temporary_directory/signature.bin"
jwt="$unsigned_token.$(base64url < "$temporary_directory/signature.bin")"

github_api() {
  local method="$1"
  local uri="$2"
  local bearer_token="$3"
  local body="${4:-}"
  local curl_arguments=(
    --fail-with-body
    --silent
    --show-error
    --request "$method"
    --url "$uri"
    --header "Accept: application/vnd.github+json"
    --header "X-GitHub-Api-Version: 2026-03-10"
    --header "User-Agent: mazda-can-telemetry-codex"
    --header "Content-Type: application/json"
    --config -
  )
  if [[ -n "$body" ]]; then
    curl_arguments+=(--data "$body")
  fi
  printf 'header = "Authorization: Bearer %s"\n' "$bearer_token" | curl "${curl_arguments[@]}"
}

app_response="$(github_api GET 'https://api.github.com/app' "$jwt")"
app_name="$(jq -er '.name' <<<"$app_response")"
app_slug="$(jq -er '.slug' <<<"$app_response")"
owner="${repository%%/*}"
repository_name="${repository#*/}"

if [[ -z "$installation_id" ]]; then
  installations="$(github_api GET 'https://api.github.com/app/installations?per_page=100' "$jwt")"
  matches="$(jq -c --arg owner "$owner" '[.[] | select(.account.login | ascii_downcase == ($owner | ascii_downcase))]' <<<"$installations")"
  match_count="$(jq -r 'length' <<<"$matches")"
  if [[ "$match_count" != "1" ]]; then
    printf "Expected one '%s' App installation for owner '%s', found %s. Set %s explicitly if necessary.\n" \
      "$role" "$owner" "$match_count" "$installation_environment" >&2
    exit 1
  fi
  installation_id="$(jq -er '.[0].id' <<<"$matches")"
fi

token_body="$(jq -cn --arg repository "$repository_name" '{repositories: [$repository]}')"
access_response="$(github_api POST "https://api.github.com/app/installations/$installation_id/access_tokens" "$jwt" "$token_body")"
installation_token="$(jq -er '.token' <<<"$access_response")"

assert_permission() {
  local permission_name="$1"
  local required="$2"
  local actual
  actual="$(jq -r --arg name "$permission_name" '.permissions[$name] // "none"' <<<"$access_response")"
  if [[ "$required" == "write" && "$actual" != "write" ]] || \
     [[ "$required" == "read" && "$actual" != "read" && "$actual" != "write" ]]; then
    printf "The '%s' App needs '%s: %s' permission, but the installation token has '%s'.\n" \
      "$role" "$permission_name" "$required" "$actual" >&2
    exit 1
  fi
}

if [[ "$role" == "coding" ]]; then
  assert_permission contents write
else
  assert_permission contents read
fi
assert_permission pull_requests write
assert_permission issues write
assert_permission metadata read

if [[ "$tool" == "check" ]]; then
  jq -n \
    --arg role "$role" \
    --arg app "$app_name" \
    --arg app_id "$app_id" \
    --arg expected_actor "$app_slug[bot]" \
    --arg installation_id "$installation_id" \
    --arg repository "$repository" \
    --arg token_expires_at "$(jq -er '.expires_at' <<<"$access_response")" \
    --argjson permissions "$(jq '.permissions' <<<"$access_response")" \
    '{role: $role, app: $app, app_id: $app_id, expected_actor: $expected_actor,
      installation_id: $installation_id, repository: $repository,
      token_expires_at: $token_expires_at, permissions: $permissions}'
  exit 0
fi

(($# > 0)) || {
  printf "Provide command arguments after --tool %s --.\n" "$tool" >&2
  exit 1
}

if [[ "$tool" == "gh" ]]; then
  GH_TOKEN="$installation_token" GITHUB_TOKEN="$installation_token" gh "$@"
else
  script_directory="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
  YK_GITHUB_APP_TOKEN="$installation_token" \
    GIT_ASKPASS="$script_directory/git-askpass.sh" \
    GIT_TERMINAL_PROMPT=0 \
    git "$@"
fi
