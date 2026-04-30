$files = Get-ChildItem -Path "src" -Recurse -Include *.cpp,*.h

foreach ($file in $files) {
    $content = Get-Content -Path $file.FullName -Raw -Encoding UTF8
    
    # Process Warning
    # \u043f\u0440\u0435\u0434\u0443\u043f\u0440\u0435\u0436\u0434\u0435\u043d\u0438\u0435 is предупреждение
    $content = [regex]::Replace($content, '(emit\s+)?logMessage\(([^;]*?(?i)(\u043f\u0440\u0435\u0434\u0443\u043f\u0440\u0435\u0436\u0434\u0435\u043d\u0438\u0435)[^;]*?)\)', {
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
    # \u043e\u0448\u0438\u0431\u043a\u0430 is ошибка
    $content = [regex]::Replace($content, '(emit\s+)?logMessage\(([^;]*?(?i)(\u043e\u0448\u0438\u0431\u043a\u0430)[^;]*?)\)', {
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
        Write-Output "Updated $($file.FullName)"
    }
}
