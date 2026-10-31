@echo off
setlocal
cd /d "%~dp0\..\.."
set "PROJECT_ROOT=%CD%"
set "UPROJECT=%PROJECT_ROOT%\MedicalGestureLib.uproject"
set "UE_CMD=C:\Program Files\Epic Games\UE_5.4\Engine\Binaries\Win64\UnrealEditor-Cmd.exe"
set "SCRIPT=%PROJECT_ROOT%\Tools\VoiceGen\configurar_paciente_ue.py"

if not exist "%UE_CMD%" (
    echo [ERRO] UnrealEditor-Cmd.exe nao encontrado em: "%UE_CMD%"
    echo Verifique o caminho de instalacao da Unreal Engine 5.4.
    pause
    exit /b 1
)

if not exist "%SCRIPT%" (
    echo [ERRO] Script nao encontrado: "%SCRIPT%"
    pause
    exit /b 1
)

tasklist /FI "IMAGENAME eq UnrealEditor.exe" 2>NUL | find /I /N "UnrealEditor.exe">NUL
if "%ERRORLEVEL%"=="0" (
    echo.
    echo ========================================================
    echo [AVISO IMPORTANTE] O Unreal Editor parece estar aberto!
    echo ========================================================
    echo Feche a Unreal Engine antes de continuar para evitar que
    echo os arquivos .uasset entrem em conflito de bloqueio.
    echo.
    echo Feche a janela da Unreal e depois pressione qualquer tecla...
    pause
)

echo ========================================================
echo Importando audios e configurando BP_Patient na Unreal...
echo ========================================================
"%UE_CMD%" "%UPROJECT%" -run=pythonscript -script="%SCRIPT%" -unattended -nullrhi

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERRO] Falha na importacao via UnrealEditor-Cmd.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo ========================================================
echo [SUCESSO] Audios, DT_FalasPaciente e BP_Patient configurados!
echo ========================================================
pause

