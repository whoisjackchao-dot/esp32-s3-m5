$audioPath = Read-Host '测试音频完整路径（WAV或MP3）'
$audioFile = Get-Item -LiteralPath $audioPath -ErrorAction Stop
if ($audioFile.PSIsContainer -or $audioFile.Length -eq 0) {
    throw '必须选择非空音频文件'
}
if ($audioFile.Extension.ToLowerInvariant() -notin @('.wav', '.mp3')) {
    throw '只接受WAV或MP3；还需自行核对实际编码与时长'
}
if ($audioFile.Length -gt 25000000) {
    throw '请使用小于25MB的短测试音频'
}
if ($audioFile.FullName.Contains('"')) {
    throw '文件路径含不支持的引号'
}
if ((Read-Host '已确认音频不超过30秒，允许上传智谱并承担可能费用？输入YES') -cne 'YES') {
    throw '已取消，未发送请求'
}
$secret = Read-Host '请输入智谱API Key（隐藏输入）' -AsSecureString
$ptr = [IntPtr]::Zero
$key = $null
$curlConfig = $null
try {
    $ptr = [Runtime.InteropServices.Marshal]::SecureStringToBSTR($secret)
    $key = [Runtime.InteropServices.Marshal]::PtrToStringBSTR($ptr)
    if ([string]::IsNullOrWhiteSpace($key) -or $key -match '[\r\n"\\]') {
        throw 'Key为空或含异常字符；请在官方控制台重新核对'
    }
    $curlConfig = 'header = "Authorization: Bearer ' + $key + '"'
    $upload = 'file=@"' + $audioFile.FullName.Replace('\', '/') + '"'
    $curlConfig | & curl.exe --config - --silent --show-error `
        --connect-timeout 10 --max-time 20 `
        --request POST `
        --url 'https://open.bigmodel.cn/api/paas/v4/audio/transcriptions' `
        --form 'model=glm-asr-2512' `
        --form 'stream=false' `
        --form $upload `
        --write-out '\nHTTP %{http_code}\n'
    $curlExit = $LASTEXITCODE
    Write-Host ('curl退出码：' + $curlExit)
} finally {
    if ($ptr -ne [IntPtr]::Zero) {
        [Runtime.InteropServices.Marshal]::ZeroFreeBSTR($ptr)
    }
    $key = $null
    $curlConfig = $null
    $secret.Dispose()
}
