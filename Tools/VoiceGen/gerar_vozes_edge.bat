@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo Gerando vozes com Edge-TTS (Antonio Neural) + Rhubarb...
echo ========================================================
.venv\Scripts\python.exe gerar_vozes_edge.py %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERRO] Falha ao gerar vozes com Edge-TTS.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCESSO] Vozes de alta qualidade e visemas gerados em output/!
echo Para importar na Unreal, feche o editor e execute: importar_na_unreal.bat

