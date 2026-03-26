import os
import tempfile
from flask import Flask, request, jsonify
from faster_whisper import WhisperModel
import wave

app = Flask(__name__)

print("Caricamento modello Whisper...")
# Usa "base" per velocità, "small" per più accuratezza
# device="cuda" se hai GPU Nvidia, altrimenti "cpu"
model = WhisperModel("base", device="cpu", compute_type="int8")
print("Modello pronto.")

@app.route('/transcribe', methods=['POST'])
def transcribe():
    try:
        # ACCETTA DATI RAW DAL BODY
        audio_data = request.get_data()
        
        if not audio_data or len(audio_data) < 44:
            return jsonify({"error": "Dati audio vuoti o troppo corti"}), 400
        
        # Salva temporaneamente come file WAV
        with tempfile.NamedTemporaryFile(delete=False, suffix=".wav") as tmp_file:
            tmp_path = tmp_file.name
            tmp_file.write(audio_data)
        
        print(f"Ricevuti {len(audio_data)} bytes di audio")
        
        # Trascrizione con faster-whisper
        segments, info = model.transcribe(tmp_path, beam_size=5, language="it")
        text = " ".join([segment.text for segment in segments]).strip()
        
        print(f"Trascritto: {text}")
        return jsonify({"text": text})
    
    except Exception as e:
        print(f"Errore server: {e}")
        import traceback
        traceback.print_exc()
        return jsonify({"error": str(e)}), 500
    
    finally:
        # Pulizia file temporaneo
        if 'tmp_path' in locals() and os.path.exists(tmp_path):
            os.remove(tmp_path)

if __name__ == '__main__':
    # 0.0.0.0 = accessibile dalla rete locale
    # Porta 5000 deve essere aperta nel firewall Windows
    print("Server avviato su http://0.0.0.0:5000")
    app.run(host='0.0.0.0', port=5000, debug=False)