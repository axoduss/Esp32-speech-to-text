🎙️ ESP32-S3 Voice Assistant con Whisper Locale
---

Sistema di riconoscimento vocale offline che cattura l'audio con ESP32-S3, invia al PC per la trascrizione con faster-whisper, e visualizza il testo su display OLED


## ✨ Caratteristiche
- 🎤 **Acquisizione audio** tramite microfono digitale INMP441 (I2S)
- 🔊 **Voice Activity Detection (VAD) adattivo**: rileva automaticamente quando inizi e smetti di parlare
- 📡 **Trasmissione WiFi** dell'audio al PC in formato WAV
- 🧠 **Trascrizione locale** con faster-whisper (nessun cloud, privacy garantita)
- 🇮🇹 **Supporto lingua italiana** configurabile
- 📺 **Visualizzazione testo** su display OLED SSD1306 128x64
- 🔋 **Gestione memoria ottimizzata**: utilizzo PSRAM per buffer audio circolari
- 🔄 **Calibrazione automatica**: adattamento delle soglie VAD al rumore ambientale



## 📦 Hardware Richiesto

- ESP32-S3 DevKit (N16R8)
- Microfono INMP441
- Display OLED SSD1306
- Cavi jumper
- Breadboard



## 💻 Software Richiesto
- ESP32 (Arduino IDE)
- Arduino IDE ≥ 2.3.8
- ESP32 Board Package ≥ 3.x (Installato via Board Manager)
- Librerie Arduino (installabili via Library Manager):
- Adafruit SSD1306
- Adafruit GFX Library


Configurazione scheda consigliata:

- Board: "ESP32S3 Dev Module"
- USB CDC On Boot: "Enabled"
- USB DFU On Boot: "Disabled"
- Flash Mode: "QIO 80MHz"
- Flash Size: "16MB"
- PSRAM: "OPI PSRAM"
- Partition Scheme: "16M Flash (3MB APP/9.9MB FATFS)"


PC Server (Python)
Requisiti Python 3.8+
> pip install flask faster-whisper numpy soundfile

Nota: faster-whisper richiede dipendenze native.
Su Windows, assicurati di avere Visual C++ Redistributable installato.
Su Linux: sudo apt install build-essential



## 🚀 Installazione
### Configurazione Server PC


Clona o scarica il repository
> cd Esp32-STT/server

(Opzionale) Crea virtual environment
>python -m venv venv
>source venv/bin/activate  # Linux/Mac
o
>venv\Scripts\activate     # Windows

Installa dipendenze
>pip install -r requirements.txt

Avvia il server
> python server_whisper.py

### Configurazione ESP32
Apri Esp32-STT.ino in Arduino IDE
Modifica le credenziali WiFi:

>const char* ssid = "NOME_TUA_RETE";
>const char* password = "TUA_PASSWORD";
>const char* serverIP = "192.168.1.XX"; // IP del tuo PC

Verifica i pin nel codice corrispondano al tuo cablaggio
Seleziona la scheda e la configurazione PSRAM come indicato sopra
Compila e carica sull'ESP32



## ▶️ Utilizzo
- Avvia il server Python sul PC e attendi: ✓ Modello Whisper caricato
- Accendi l'ESP32: si connetterà al WiFi e mostrerà "In ascolto..." sul display
- Calibrazione automatica: i primi 1-2 secondi il sistema misura il rumore ambientale
- Parla chiaramente vicino al microfono:
- Il VAD rileverà l'inizio del parlato
- Attenderà ~800ms di silenzio per confermare la fine della frase
- Invierà l'audio al PC per la trascrizione
- Il testo trascritto apparirà sul display OLED e nel Monitor Seriale



Output Seriale di Esempio	
=== Sistema VAD pronto ===
Calibrazione: noise=320, silence=480, speech=640
>>> PARLATO RILEVATO - Inizio registrazione
>>> Fine parlato. Totale: 48000 campioni (3.0s)
Invio 96044 bytes audio...
✓ Trascritto in 2.1s: 'accendi la luce del salotto'
Risultato: accendi la luce del salotto
State:0 RMS:245 FreePSRAM:7850KB



## ⚙️ Parametri Configurabili
VAD (Voice Activity Detection)
|      Component     | Quantity |                      Notes                      |   |   |
|:------------------:|:--------:|:-----------------------------------------------:|---|---|
| ESP32-S3 Dev Board | 1x       | Any ESP32-S3 variant (e.g., ESP32-S3-DevKitC-1) |   |   |
| INMP441 Microphone | 1x       | I2S omnidirectional MEMS microphone             |   |   |
| Jumper Wires       | 5x       | For connecting microphone to ESP32              |   |   |
| USB Cable          | 1x       | For programming and power                       |   |   |




## 🔄 Estensioni Possibili
- Wake-Up Word: Aggiungi rilevamento locale di "Hey ESP" con TensorFlow Lite Micro
- Comandi vocali: Parser locale per azioni come "avanti", "stop" → controllo motore
- Feedback audio: Buzzer o sintesi vocale locale per conferma comandi
- Deep Sleep: Risparmio energetico tra una richiesta e l'altra
- Multi-lingua: Selezione dinamica della lingua per Whisper via comando


## 🤝 Contributi
I contributi sono benvenuti!

Made with ❤️ for the maker community
