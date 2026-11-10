@echo off
chcp 65001 > nul
echo ========================================================
echo  Testador da API de Avaliacao por Voz (voice-eval-api)
echo ========================================================
python "%~dp0testar_api.py"
pause
