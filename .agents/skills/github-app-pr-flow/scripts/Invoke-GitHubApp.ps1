[CmdletBinding(PositionalBinding = $false)]
param(
    [Parameter(Mandatory = $true)]
    [ValidateSet("coding", "reviewer")]
    [string]$Role,

    [ValidateSet("check", "gh", "git")]
    [string]$Tool = "check",

    [ValidatePattern("^[A-Za-z0-9_.-]+/[A-Za-z0-9_.-]+$")]
    [string]$Repository = "Yuke-hd/mazda-can-telemetry",

    [Parameter(ValueFromRemainingArguments = $true)]
    [string[]]$CommandArguments
)

Set-StrictMode -Version Latest
$ErrorActionPreference = "Stop"

$roleConfig = @{
    coding = @{
        DefaultAppId = "4559002"
        AppIdEnvironment = "YK_CODING_APP_ID"
        KeyPathEnvironment = "YK_CODING_PRIVATE_KEY_PATH"
        InstallationEnvironment = "YK_CODING_INSTALLATION_ID"
    }
    reviewer = @{
        DefaultAppId = "4559033"
        AppIdEnvironment = "YK_REVIEWER_APP_ID"
        KeyPathEnvironment = "YK_REVIEWER_PRIVATE_KEY_PATH"
        InstallationEnvironment = "YK_REVIEWER_INSTALLATION_ID"
    }
}

$apiHeaders = @{
    Accept = "application/vnd.github+json"
    "X-GitHub-Api-Version" = "2026-03-10"
    "User-Agent" = "mazda-can-telemetry-codex"
}

function ConvertTo-Base64Url {
    param([Parameter(Mandatory = $true)][byte[]]$Bytes)

    return [Convert]::ToBase64String($Bytes).TrimEnd("=").Replace("+", "-").Replace("/", "_")
}

function Get-ConfiguredValue {
    param(
        [Parameter(Mandatory = $true)][string]$Name,
        [string]$DefaultValue
    )

    $value = [Environment]::GetEnvironmentVariable($Name)
    if ([string]::IsNullOrWhiteSpace($value)) {
        return $DefaultValue
    }
    return $value
}

function New-GitHubAppJwt {
    param(
        [Parameter(Mandatory = $true)][string]$AppId,
        [Parameter(Mandatory = $true)][string]$PrivateKeyPath
    )

    if (-not (Test-Path -LiteralPath $PrivateKeyPath -PathType Leaf)) {
        throw "Private key file not found. Check the configured private-key path for role '$Role'."
    }

    $now = [DateTimeOffset]::UtcNow.ToUnixTimeSeconds()
    $headerJson = @{ alg = "RS256"; typ = "JWT" } | ConvertTo-Json -Compress
    $payloadJson = @{ iat = $now - 60; exp = $now + 540; iss = $AppId } | ConvertTo-Json -Compress
    $headerPart = ConvertTo-Base64Url ([Text.Encoding]::UTF8.GetBytes($headerJson))
    $payloadPart = ConvertTo-Base64Url ([Text.Encoding]::UTF8.GetBytes($payloadJson))
    $unsignedToken = "$headerPart.$payloadPart"

    $rsa = [Security.Cryptography.RSA]::Create()
    try {
        $privateKeyPem = [IO.File]::ReadAllText((Resolve-Path -LiteralPath $PrivateKeyPath))
        $rsa.ImportFromPem($privateKeyPem)
        $signature = $rsa.SignData(
            [Text.Encoding]::UTF8.GetBytes($unsignedToken),
            [Security.Cryptography.HashAlgorithmName]::SHA256,
            [Security.Cryptography.RSASignaturePadding]::Pkcs1
        )
        return "$unsignedToken.$(ConvertTo-Base64Url $signature)"
    }
    finally {
        $rsa.Dispose()
    }
}

function Invoke-GitHubRest {
    param(
        [Parameter(Mandatory = $true)][ValidateSet("GET", "POST")][string]$Method,
        [Parameter(Mandatory = $true)][string]$Uri,
        [Parameter(Mandatory = $true)][string]$BearerToken,
        [string]$Body
    )

    $headers = $apiHeaders.Clone()
    $headers.Authorization = "Bearer $BearerToken"
    $parameters = @{
        Method = $Method
        Uri = $Uri
        Headers = $headers
        ContentType = "application/json"
    }
    if (-not [string]::IsNullOrWhiteSpace($Body)) {
        $parameters.Body = $Body
    }
    return Invoke-RestMethod @parameters
}

function Get-GitHubAppSession {
    $config = $roleConfig[$Role]
    $appId = Get-ConfiguredValue $config.AppIdEnvironment $config.DefaultAppId
    $keyPath = Get-ConfiguredValue $config.KeyPathEnvironment $null
    if ([string]::IsNullOrWhiteSpace($keyPath)) {
        throw "Set $($config.KeyPathEnvironment) to the local private-key file for role '$Role'."
    }

    $jwt = New-GitHubAppJwt $appId $keyPath
    $app = Invoke-GitHubRest GET "https://api.github.com/app" $jwt
    $installationId = Get-ConfiguredValue $config.InstallationEnvironment $null
    $owner, $repositoryName = $Repository.Split("/", 2)

    if ([string]::IsNullOrWhiteSpace($installationId)) {
        $installations = @(Invoke-GitHubRest GET "https://api.github.com/app/installations?per_page=100" $jwt)
        $matches = @($installations | Where-Object { $_.account.login -ieq $owner })
        if ($matches.Count -ne 1) {
            throw "Expected one '$Role' App installation for owner '$owner', found $($matches.Count). Set $($config.InstallationEnvironment) explicitly if necessary."
        }
        $installationId = [string]$matches[0].id
    }

    $requestedPermissions = if ($Role -eq "coding") {
        @{ contents = "write"; pull_requests = "write"; issues = "write"; metadata = "read" }
    }
    else {
        @{ contents = "read"; pull_requests = "write"; issues = "write"; metadata = "read" }
    }
    $tokenBody = @{ repositories = @($repositoryName); permissions = $requestedPermissions } | ConvertTo-Json -Compress
    $access = Invoke-GitHubRest POST "https://api.github.com/app/installations/$installationId/access_tokens" $jwt $tokenBody
    return @{
        AppId = $appId
        AppName = [string]$app.name
        AppSlug = [string]$app.slug
        InstallationId = $installationId
        Token = [string]$access.token
        ExpiresAt = [string]$access.expires_at
        Permissions = $access.permissions
    }
}

function Invoke-WithTemporaryEnvironment {
    param(
        [Parameter(Mandatory = $true)][hashtable]$Values,
        [Parameter(Mandatory = $true)][scriptblock]$Action
    )

    $saved = @{}
    foreach ($name in $Values.Keys) {
        $saved[$name] = [Environment]::GetEnvironmentVariable($name, "Process")
        [Environment]::SetEnvironmentVariable($name, [string]$Values[$name], "Process")
    }
    try {
        & $Action
    }
    finally {
        foreach ($name in $Values.Keys) {
            [Environment]::SetEnvironmentVariable($name, $saved[$name], "Process")
        }
    }
}

function Assert-GitHubPermissions {
    param([Parameter(Mandatory = $true)]$Permissions)

    $required = if ($Role -eq "coding") {
        @{ contents = "write"; pull_requests = "write"; issues = "write"; metadata = "read" }
    }
    else {
        @{ contents = "read"; pull_requests = "write"; issues = "write"; metadata = "read" }
    }
    $level = @{ read = 1; write = 2 }
    foreach ($permissionName in $required.Keys) {
        $property = $Permissions.PSObject.Properties[$permissionName]
        $actual = if ($null -eq $property) { "none" } else { [string]$property.Value }
        if (-not $level.ContainsKey($actual) -or $level[$actual] -lt $level[$required[$permissionName]]) {
            throw "The '$Role' App needs '${permissionName}: $($required[$permissionName])' permission, but the installation token has '$actual'."
        }
    }

    $allowedWritePermissions = if ($Role -eq "coding") {
        @("contents", "pull_requests", "issues")
    }
    else {
        @("pull_requests", "issues")
    }
    foreach ($property in $Permissions.PSObject.Properties) {
        if ([string]$property.Value -eq "write" -and $property.Name -notin $allowedWritePermissions) {
            throw "The '$Role' App token has unexpected write permission '$($property.Name)'."
        }
    }
}

$session = Get-GitHubAppSession
Assert-GitHubPermissions $session.Permissions

if ($Tool -eq "check") {
    [pscustomobject]@{
        role = $Role
        app = $session.AppName
        app_id = $session.AppId
        expected_actor = "$($session.AppSlug)[bot]"
        installation_id = $session.InstallationId
        repository = $Repository
        token_expires_at = $session.ExpiresAt
        permissions = $session.Permissions
    } | ConvertTo-Json -Depth 8
    exit 0
}

if ($CommandArguments.Count -eq 0) {
    throw "Provide command arguments after -Tool $Tool."
}

if ($Tool -eq "gh") {
    Invoke-WithTemporaryEnvironment @{
        GH_TOKEN = $session.Token
        GITHUB_TOKEN = $session.Token
    } {
        & gh @CommandArguments
        if ($LASTEXITCODE -ne 0) {
            throw "gh exited with code $LASTEXITCODE."
        }
    }
    exit 0
}

$askPassPath = Join-Path $PSScriptRoot "git-askpass.cmd"
Invoke-WithTemporaryEnvironment @{
    YK_GITHUB_APP_TOKEN = $session.Token
    GIT_ASKPASS = $askPassPath
    GIT_TERMINAL_PROMPT = "0"
} {
    & git @CommandArguments
    if ($LASTEXITCODE -ne 0) {
        throw "git exited with code $LASTEXITCODE."
    }
}
