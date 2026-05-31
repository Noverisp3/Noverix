@echo off
call make
if %errorlevel% neq 0 exit /b %errorlevel%
"C:\Program Files\Bochs-3.0\bochs.exe" -q -f bochsrc.bxrc
