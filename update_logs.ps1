$files = Get-ChildItem -Path "src" -Recurse -Include *.cpp,*.h

foreach ($file in $files) {
    $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8
    $modified = $false
    
    # Process Warning
    $content = [regex]::Replace($content, '(emit\s+)?logMessage\(([^;]*?(?i)предупреждение[^;]*?)\)', {
        param($match)
        $args_str = $match.Groups[2].Value
        if ($args_str -match 'LogLevel::') { return $match.Groups[0].Value }
        
        if ($args_str -match 'LogCategory::') {
            $new_args = $args_str + ", LogLevel::Warning"
        } else {
            $new_args = $args_str + ", LogCategory::APP, LogLevel::Warning"
        }
        return $match.Groups[1].Value + "logMessage(" + $new_args + ")"
    })
    
    # Process Error
    $content = [regex]::Replace($content, '(emit\s+)?logMessage\(([^;]*?(?i)(ошибка|ошибк)[^;]*?)\)', {
        param($match)
        $args_str = $match.Groups[2].Value
        if ($args_str -match 'LogLevel::') { return $match.Groups[0].Value }
        
        if ($args_str -match 'LogCategory::') {
            $new_args = $args_str + ", LogLevel::Error"
        } else {
            $new_args = $args_str + ", LogCategory::APP, LogLevel::Error"
        }
        return $match.Groups[1].Value + "logMessage(" + $new_args + ")"
    })
    
    if ((Get-Content -Path $file.FullName -Raw -Encoding UTF8) -ne $content) {
        Set-Content -Path $file.FullName -Value $content -Encoding UTF8 -NoNewline
        Write-Host "Updated $($file.FullName)"
    }
}
