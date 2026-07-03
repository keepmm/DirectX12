@echo off
call "C:\Program Files\Microsoft Visual Studio\2022\Community\Common7\Tools\VsDevCmd.bat"
msbuild "%~dp0Scripts\Scripts.vcxproj" /p:Configuration=Debug /p:Platform=x64 /p:SolutionDir=%~dp0
pause