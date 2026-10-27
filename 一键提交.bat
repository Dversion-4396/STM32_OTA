@echo off
chcp 65001 >nul
title STM32_OTA 一键提交推送

echo ========================================
echo   STM32_OTA 一键提交推送
echo ========================================
echo.

cd /d "%~dp0"

:: 显示当前分支
echo [当前分支]
git branch --show-current
echo.

:: 显示修改
echo [修改的文件]
git status --short
echo.

:: 检查是否有修改
git status --porcelain > "%TEMP%\git_check.txt"
for %%A in ("%TEMP%\git_check.txt") do if %%~zA==0 (
    echo [提示] 没有修改，无需提交
    pause
    exit /b 0
)

:: 输入提交信息
echo.
set /p MSG=请输入提交说明（如: feat(A) 实现CAT1_SendAT）: 

if "%MSG%"=="" (
    echo [错误] 提交说明不能为空
    pause
    exit /b 1
)

echo.
echo ========================================
echo 开始执行：add -^> commit -^> pull -^> push
echo ========================================
echo.

echo [1/4] 添加文件...
git add -A
if errorlevel 1 goto FAIL

echo [2/4] 提交...
git commit -m "%MSG%"
if errorlevel 1 goto FAIL

echo [3/4] 拉取最新代码...
git pull --rebase
if errorlevel 1 (
    echo.
    echo [警告] pull 失败，可能有冲突
    echo 请手动解决冲突后再执行 git rebase --continue
    pause
    exit /b 1
)

echo [4/4] 推送到 GitHub...
git push
if errorlevel 1 goto FAIL

echo.
echo ========================================
echo  ✅ 完成！代码已推送到 GitHub
echo ========================================
echo.
git log --oneline -3
echo.
pause
exit /b 0

:FAIL
echo.
echo ========================================
echo  ❌ 执行失败，请查看上面的错误信息
echo ========================================
pause
exit /b 1
