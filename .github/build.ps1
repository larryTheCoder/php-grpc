$ErrorActionPreference = "Stop"

$env:PATH = "C:\php\devel;C:\php\bin;C:\php\deps\bin;$env:PATH"

$task = New-Item 'task.bat' -Force
Add-Content $task 'call phpize 2>&1'
Add-Content $task "call configure --with-php-build=C:\php\devel --with-openssl --enable-grpc 2>&1"
Add-Content $task 'nmake /nologo 2>&1'
Add-Content $task 'exit %errorlevel%'
& "C:\php\php-sdk-2.2.0\phpsdk-vs16-x64.bat" -t $task
if (-not $?) {
    throw "building failed with errorlevel $LastExitCode"
}

Copy-Item "x64\Release_TS\php_grpc.dll" "E:\ACD-ANG-Production-PM5\bin\php\ext\php_grpc.dll"

Remove-Item config.nice.bat
Remove-Item configure.bat
Remove-Item configure.js
Remove-Item Makefile
Remove-Item Makefile.objects
Remove-Item run-tests.php
Remove-Item task.bat

