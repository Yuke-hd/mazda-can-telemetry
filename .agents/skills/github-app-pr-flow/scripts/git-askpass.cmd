@echo off
echo %~1 | findstr /I "username" >nul
if not errorlevel 1 (
  echo x-access-token
) else (
  echo %YK_GITHUB_APP_TOKEN%
)
