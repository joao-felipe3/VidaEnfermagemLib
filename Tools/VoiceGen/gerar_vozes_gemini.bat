@echo off
setlocal
cd /d "%~dp0"

echo ========================================================
echo Gerando vozes com Google AI Studio (Gemini 3.8 Flash TTS)
echo Voz: Puck (Jovem de 24 anos) + Rhubarb Lip Sync
echo ========================================================
.venv\Scripts\python.exe gerar_vozes_gemini.py %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERRO] Falha ao gerar vozes com Gemini.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCESSO] Vozes e visemas gerados em Tools/VoiceGen/output/!
echo Feche o Unreal Editor e execute: importar_na_unreal.bat

