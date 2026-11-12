"""Script de validação e teste da API de Avaliação de Fala (voice-eval-api)
Testa a conectividade, saúde do servidor no Render, cenários cadastrados e simula avaliações.
"""

import sys
import json
import urllib.request
import urllib.error
import time

API_URL = "https://voice-eval-api.onrender.com"
API_KEY = "wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg"

def log(msg):
    print(f"[VOICE-EVAL] {msg}")

def check_health():
    url = f"{API_URL}/health"
    log(f"Testando conexão com {url}...")
    start = time.time()
    try:
        req = urllib.request.Request(url, headers={"User-Agent": "VoiceEvalTester/1.0"})
        with urllib.request.urlopen(req, timeout=35) as resp:
            elapsed = (time.time() - start) * 1000
            data = json.loads(resp.read().decode())
            log(f"Servidor acordado! Status={resp.status} em {elapsed:.1f}ms: {data}")
            return True
    except Exception as e:
        log(f"Erro ao conectar (servidor pode estar inicializando a frio): {e}")
        return False

def list_scenarios():
    url = f"{API_URL}/v1/scenarios"
    req = urllib.request.Request(url, headers={"x-api-key": API_KEY, "User-Agent": "VoiceEvalTester/1.0"})
    with urllib.request.urlopen(req, timeout=30) as resp:
        scenarios = json.loads(resp.read().decode())
        log(f"Cenários encontrados ({len(scenarios)}):")
        for sc in scenarios:
            print(f"  - ID: {sc['id']} | Título: {sc['title']} | Idioma: {sc['language']} | Perguntas: {sc['question_count']}")
        return scenarios

def test_evaluation(scenario_id, question_id, transcript_text):
    url = f"{API_URL}/v1/evaluate/text"
    payload = json.dumps({
        "scenario_id": scenario_id,
        "question_id": question_id,
        "transcript": transcript_text
    }).encode("utf-8")
    
    log(f"\n--- Testando Pergunta '{question_id}' ---")
    log(f"Fala enviada: \"{transcript_text}\"")
    
    req = urllib.request.Request(url, data=payload, headers={
        "x-api-key": API_KEY,
        "Content-Type": "application/json",
        "User-Agent": "VoiceEvalTester/1.0"
    })
    
    start = time.time()
    with urllib.request.urlopen(req, timeout=30) as resp:
        elapsed = (time.time() - start) * 1000
        res = json.loads(resp.read().decode())
        log(f"Resultado (em {elapsed:.0f}ms):")
        print(f"  * Status: {res.get('status')}")
        print(f"  * Aprovado: {res.get('passed')}")
        print(f"  * Nota: {res.get('score', 0):.2f}")
        print(f"  * Feedback: {res.get('feedback')}")
        if res.get('criteria'):
            print(f"  * Critérios:")
            for c in res['criteria']:
                status = "OK" if c['met'] else "FALHOU"
                req_tag = "(Obrigatório)" if c['required'] else "(Opcional)"
                print(f"    - [{status}] {c['id']} {req_tag}: Evidência='{c.get('evidence', '')}'")
        if res.get('critical_errors'):
            print(f"  * Erros Críticos:")
            for err in res['critical_errors']:
                print(f"    - {err['id']}: {err['description']}")
        return res

if __name__ == "__main__":
    if not check_health():
        sys.exit(1)
        
    list_scenarios()
    
    # Simula as etapas principais da conversa clínica
    testes = [
        ("apresentacao", "Olá, bom dia! Eu sou o enfermeiro Lucas e vou realizar sua coleta de sangue hoje. Como o senhor está se sentindo?"),
        ("identificacao-paciente", "O senhor poderia por favor me dizer o seu nome completo e a sua data de nascimento?"),
        ("alergias", "O senhor sabe se tem alguma alergia a látex da luva, álcool ou soluções de iodo e clorexidina?"),
        ("consentimento-sim", "O senhor autoriza que façamos a coleta do seu sangue agora?")
    ]
    
    for q_id, fala in testes:
        test_evaluation("coleta-sangue", q_id, fala)

