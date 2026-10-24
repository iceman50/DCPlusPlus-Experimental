# Tests the credentials against MaxMind without downloading a database.
# The key is entered privately, never supplied on the command line or saved.
[CmdletBinding()]
param()

$ErrorActionPreference = 'Stop'
$accountId = (Read-Host 'MaxMind account ID (Account Information page)').Trim()
if ($accountId -notmatch '^[0-9]+$') {
    Write-Output 'Account ID must contain digits only.'
    exit 1
}

$secureKey = Read-Host 'Full MaxMind license key' -AsSecureString
$keyPointer = [IntPtr]::Zero
$response = $null
$request = $null
$previousProtocols = [Net.ServicePointManager]::SecurityProtocol
try {
    $keyPointer = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secureKey)
    $plainKey = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($keyPointer).Trim()
    if ([string]::IsNullOrEmpty($plainKey)) {
        Write-Output 'License key is empty.'
        exit 1
    }
    $authBytes = [Text.Encoding]::UTF8.GetBytes($accountId + ':' + $plainKey)
    [Net.ServicePointManager]::SecurityProtocol = [Net.SecurityProtocolType]::Tls12
    $request = [Net.HttpWebRequest]::Create('https://download.maxmind.com/geoip/databases/GeoLite2-Country/download?suffix=tar.gz')
    $request.Method = 'HEAD'
    $request.AllowAutoRedirect = $false
    $request.Timeout = 20000
    $request.Headers['Authorization'] = 'Basic ' + [Convert]::ToBase64String($authBytes)
    [Array]::Clear($authBytes, 0, $authBytes.Length)
    $plainKey = $null
    try {
        $response = $request.GetResponse()
    } catch [Net.WebException] {
        if ($null -eq $_.Exception.Response) {
            Write-Output ('Connection failed: ' + $_.Exception.Status + '. No credentials were printed or saved.')
            exit 1
        }
        $response = $_.Exception.Response
    }
    $status = [int]$response.StatusCode
    switch ($status) {
        200 { Write-Output 'HTTP 200: MaxMind accepted these credentials for GeoLite2 Country.' }
        302 { Write-Output 'HTTP 302: MaxMind accepted the request and redirected to the database download.' }
        401 { Write-Output 'HTTP 401: the direct request was also rejected. Check the account ID and full active license key in MaxMind.' }
        403 { Write-Output 'HTTP 403: access to this download was denied. Check your account access with MaxMind.' }
        407 { Write-Output 'HTTP 407: the proxy requires authentication.' }
        429 { Write-Output 'HTTP 429: MaxMind has rate-limited requests. Wait before retrying.' }
        default { Write-Output ('MaxMind check returned HTTP ' + $status + '.') }
    }
    Write-Output 'No database downloaded. You can share the result above; do not share the key.'
} catch {
    # Exception text can contain request details. Report only a fixed message.
    Write-Output 'Could not complete the check. No credentials were printed or saved.'
    exit 1
} finally {
    if ($null -ne $response) { $response.Close() }
    if ($null -ne $request) {
        $request.Headers.Remove('Authorization')
        $request.Abort()
    }
    if ($keyPointer -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($keyPointer)
    }
    if ($null -ne $authBytes) { [Array]::Clear($authBytes, 0, $authBytes.Length) }
    $plainKey = $null
    $secureKey.Dispose()
    [Net.ServicePointManager]::SecurityProtocol = $previousProtocols
}
