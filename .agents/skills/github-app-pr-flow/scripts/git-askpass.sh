#!/usr/bin/env sh

case "${1:-}" in
  *[Uu]sername*) printf '%s\n' 'x-access-token' ;;
  *) printf '%s\n' "$YK_GITHUB_APP_TOKEN" ;;
esac
