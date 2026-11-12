import urllib.request
import json

def test_eval(qid, text):
    data = json.dumps({'scenario_id': 'coleta-sangue', 'question_id': qid, 'transcript': text}).encode()
    req = urllib.request.Request('https://voice-eval-api.onrender.com/v1/evaluate/text', data=data, headers={'x-api-key': 'wcP90Sipv_00QbUM8O_NBDsALc76AJSX6bt1A2aXLtg', 'Content-Type': 'application/json'})
    with urllib.request.urlopen(req, timeout=15) as resp:
        return json.loads(resp.read().decode())

exp_phrases = [
    'O senhor já fez alguma coleta de sangue a vácuo?',
    'Se sim, prosseguir diretamente para o próximo item.',
    'O senhor já realizou coleta a vácuo? Que ótimo, então vamos prosseguir para o procedimento.',
    'Como o senhor já conhece a coleta a vácuo, vou prosseguir diretamente para a punção.',
    'O senhor já tem experiência com coleta a vácuo, podemos prosseguir diretamente para o próximo passo.',
    'O senhor já realizou coleta de sangue a vácuo anteriormente? Vou prosseguir com o procedimento.'
]

print("=== EXPERIENCIA PREVIA SIM ===")
for p in exp_phrases:
    res = test_eval('experiencia-previa-sim', p)
    print(f"P: {p}\n  Passed: {res.get('passed')} (Score: {res.get('score')})\n  Feedback: {res.get('feedback')}\n")

cons_phrases = [
    'O senhor autoriza que eu colete o sangue necessário para os seus exames?',
    'Se sim, prosseguir para o próximo item.',
    'O senhor autoriza a coleta do seu sangue? Obrigado pelo consentimento, vou prosseguir para a coleta.',
    'Perfeito, como o senhor autorizou, podemos prosseguir para o procedimento.',
    'Obrigado por autorizar, vou prosseguir com a coleta de sangue agora.',
    'O senhor autoriza a coleta? Com sua autorização, vamos iniciar o procedimento.'
]

print("=== CONSENTIMENTO SIM ===")
for p in cons_phrases:
    res = test_eval('consentimento-sim', p)
    print(f"P: {p}\n  Passed: {res.get('passed')} (Score: {res.get('score')})\n  Feedback: {res.get('feedback')}\n")

