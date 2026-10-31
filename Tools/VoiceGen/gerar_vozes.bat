@echo off
setlocal
cd /d "%~dp0"

if not exist ".venv\Scripts\python.exe" (
    echo [INFO] Ambiente virtual .venv nao encontrado. Criando...
    py -3.11 -m venv .venv 2>nul || python -m venv .venv
    echo [INFO] Instalando dependencias...
    .venv\Scripts\python.exe -m pip install -r requirements.txt
)

echo ========================================================
echo Gerando vozes com Piper TTS e visemas do Rhubarb...
echo ========================================================
.venv\Scripts\python.exe gerar_vozes.py %*

if %ERRORLEVEL% neq 0 (
    echo.
    echo [ERRO] Falha ao gerar vozes. Verifique as mensagens acima.
    pause
    exit /b %ERRORLEVEL%
)

echo.
echo [SUCESSO] Audios WAV e DT_FalasPaciente.csv gerados em Tools/VoiceGen/output/!
echo Para importar na Unreal, execute: importar_na_unreal.bat (com Unreal fechada).

