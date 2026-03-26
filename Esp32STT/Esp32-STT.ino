#include <WiFi.h>
#include <HTTPClient.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "driver/i2s.h"
#include <string.h>
#include <math.h>

// --- CONFIGURAZIONE WIFI ---
const char* ssid = "SSID";
const char* password = "PWD";
const char* serverIP = "192.168.1.8"; // IP DEL TUO PC
const int serverPort = 5000;

// --- PINOUT ---
#define I2S_WS  5
#define I2S_SD  4
#define I2S_SCK 6
#define OLED_SDA 1
#define OLED_SCL 2

// --- AUDIO CONFIG ---
#define SAMPLE_RATE 16000
#define SAMPLE_BITS 16
#define CHANNELS 1
#define BYTES_PER_SAMPLE (SAMPLE_BITS / 8)

// --- VAD PARAMETERS ---
#define VAD_FRAME_MS 50              // Analisi ogni 50ms
#define VAD_FRAME_SAMPLES (SAMPLE_RATE * VAD_FRAME_MS / 1000)  // 800 samples
int16_t SILENCE_THRESHOLD=800;        // RMS sotto questo = silenzio (adattivo)
int16_t SPEECH_THRESHOLD=1500;        // RMS sopra questo = parlato
#define MIN_SPEECH_FRAMES 4          // Min 4 frame (200ms) per confermare parlato
#define END_OF_SPEECH_FRAMES 16      // 16 frame = 800ms silenzio = fine parlato
#define PRE_ROLL_FRAMES 10           // Tieni 10 frame (500ms) prima dello start
#define MAX_RECORDING_FRAMES 200     // Max 10 secondi di registrazione

// --- BUFFER IN PSRAM ---
#define CIRCULAR_BUFFER_SECONDS 12   // Leggermente più di MAX per sicurezza
#define CIRCULAR_BUFFER_SIZE (SAMPLE_RATE * CIRCULAR_BUFFER_SECONDS * BYTES_PER_SAMPLE)

// --- STATI VAD ---
enum VadState {
  VAD_IDLE,           // In attesa, calibra soglia
  VAD_DETECTING,      // Rilevato potenziale parlato
  VAD_RECORDING,      // Registrazione attiva
  VAD_SENDING         // Invio in corso
};

// --- GLOBALI ---
Adafruit_SSD1306 display(128, 64, &Wire, -1);
int16_t* circularBuffer = nullptr;  // PSRAM
size_t cbWritePos = 0;
size_t cbTotalSamples = 0;

VadState currentState = VAD_IDLE;
size_t speechStartPos = 0;          // Posizione inizio parlato nel circular buffer
size_t recordedSamples = 0;         // Campioni da inviare
int consecutiveSilenceFrames = 0;
int consecutiveSpeechFrames = 0;
int calibrationFrames = 0;
uint32_t calibrationSum = 0;

// --- I2S CONFIG ---
i2s_pin_config_t pin_config = {
  .bck_io_num = I2S_SCK,
  .ws_io_num = I2S_WS,
  .data_out_num = I2S_PIN_NO_CHANGE,
  .data_in_num = I2S_SD
};

i2s_config_t i2s_config = {
  .mode = (i2s_mode_t)(I2S_MODE_MASTER | I2S_MODE_RX),
  .sample_rate = SAMPLE_RATE,
  .bits_per_sample = I2S_BITS_PER_SAMPLE_16BIT,
  .channel_format = I2S_CHANNEL_FMT_ONLY_LEFT,
  .communication_format = I2S_COMM_FORMAT_STAND_I2S,
  .intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,
  .dma_buf_count = 8,
  .dma_buf_len = 1024,
  .use_apll = false,
  .tx_desc_auto_clear = false,
  .fixed_mclk = 0
};

// --- FUNZIONI UTILI ---

// Calcola RMS (Root Mean Square) di un buffer di campioni
int16_t calculateRMS(int16_t* buffer, size_t len) {
  if (len == 0) return 0;
  int64_t sum = 0;
  for (size_t i = 0; i < len; i++) {
    int32_t val = buffer[i];
    sum += val * val;
  }
  return (int16_t)sqrt((double)sum / len);
}

// Legge campioni dal circular buffer (gestisce wrap-around)
void readFromCircularBuffer(int16_t* dest, size_t startIdx, size_t count) {
  size_t totalSamples = CIRCULAR_BUFFER_SIZE / sizeof(int16_t);
  for (size_t i = 0; i < count; i++) {
    size_t srcIdx = (startIdx + i) % totalSamples;
    dest[i] = circularBuffer[srcIdx];
  }
}

// Crea header WAV minimale
void createWavHeader(uint8_t* header, size_t dataSize) {
  memset(header, 0, 44);
  memcpy(header, "RIFF", 4);
  uint32_t fileSize = dataSize + 36;
  header[4] = fileSize & 0xFF; header[5] = (fileSize >> 8) & 0xFF;
  header[6] = (fileSize >> 16) & 0xFF; header[7] = (fileSize >> 24) & 0xFF;
  memcpy(header + 8, "WAVEfmt ", 8);
  header[16] = 16; header[17] = 0; header[18] = 0; header[19] = 0;  // Subchunk1Size
  header[20] = 1; header[21] = 0;  // PCM
  header[22] = 1; header[23] = 0;  // Mono
  header[24] = SAMPLE_RATE & 0xFF; header[25] = (SAMPLE_RATE >> 8) & 0xFF;
  header[26] = (SAMPLE_RATE >> 16) & 0xFF; header[27] = (SAMPLE_RATE >> 24) & 0xFF;
  uint32_t byteRate = SAMPLE_RATE * CHANNELS * BYTES_PER_SAMPLE;
  header[28] = byteRate & 0xFF; header[29] = (byteRate >> 8) & 0xFF;
  header[30] = (byteRate >> 16) & 0xFF; header[31] = (byteRate >> 24) & 0xFF;
  header[32] = CHANNELS * BYTES_PER_SAMPLE; header[33] = 0;  // BlockAlign
  header[34] = SAMPLE_BITS; header[35] = 0;  // BitsPerSample
  memcpy(header + 36, "data", 4);
  header[40] = dataSize & 0xFF; header[41] = (dataSize >> 8) & 0xFF;
  header[42] = (dataSize >> 16) & 0xFF; header[43] = (dataSize >> 24) & 0xFF;
}

// Invia audio al server e ricevi risposta
String sendAudioToServer(int16_t* audioData, size_t sampleCount) {
  if (WiFi.status() != WL_CONNECTED) return "WiFi offline";
  
  size_t dataSize = sampleCount * sizeof(int16_t);
  size_t totalSize = 44 + dataSize;
  
  // Alloca buffer temporaneo in PSRAM per WAV completo
  uint8_t* wavBuffer = (uint8_t*)heap_caps_malloc(totalSize, MALLOC_CAP_SPIRAM);
  if (!wavBuffer) {
    Serial.println("Errore: malloc PSRAM fallito per wavBuffer");
    return "Err: memoria";
  }
  
  createWavHeader(wavBuffer, dataSize);
  memcpy(wavBuffer + 44, audioData, dataSize);
  
  HTTPClient http;
  String url = "http://" + String(serverIP) + ":" + String(serverPort) + "/transcribe";
  http.begin(url);
  http.addHeader("Content-Type", "audio/wav");
  http.setConnectTimeout(5000);
  http.setTimeout(30000);  // Whisper può impiegare tempo
  
  Serial.printf("Invio %d bytes audio...\n", totalSize);
  int code = http.POST(wavBuffer, totalSize);
  
  String result;
  if (code == 200) {
    String resp = http.getString();
    // Parsing JSON minimale
    int start = resp.indexOf("\"text\"") + 8;
    int end = resp.indexOf("\"", start);
    if (start > 7 && end > start) {
      result = resp.substring(start, end);
      result.replace("\\n", " ");
    } else {
      result = "Parse err";
    }
  } else {
    result = "HTTP:" + String(code);
    if (code > 0) {
      String err = http.getString();
      Serial.println("Errore server: " + err);
    }
  }
  
  http.end();
  heap_caps_free(wavBuffer);  // Libera SUBITO per evitare frammentazione
  return result;
}

// Visualizza stato su OLED
void updateDisplay(const char* status, const char* text = nullptr) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);
  display.setCursor(0, 0);
  display.println(status);
  
  if (text && strlen(text) > 0) {
    display.setCursor(0, 12);
    // Word wrap semplice
    int lineLen = 0;
    for (int i = 0; text[i] && i < 100; i++) {
      display.write(text[i]);
      lineLen++;
      if (lineLen >= 20) {
        display.println();
        lineLen = 0;
      }
    }
  }
  display.display();
}

// --- SETUP ---
void setup() {
  Serial.begin(115200);
   
  // Display
  Wire.begin(OLED_SDA, OLED_SCL);
  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Display init fallito");
    while (1);
  }
  display.clearDisplay();
  display.setTextColor(SSD1306_WHITE);
  updateDisplay("Avvio...");
  
  // Alloca circular buffer in PSRAM
  circularBuffer = (int16_t*)heap_caps_malloc(CIRCULAR_BUFFER_SIZE, MALLOC_CAP_SPIRAM);
  if (!circularBuffer) {
    Serial.println("ERRORE CRITICO: PSRAM non disponibile!");
    updateDisplay("ERR: PSRAM");
    while (1);
  }
  Serial.printf("Circular buffer: %d KB in PSRAM\n", CIRCULAR_BUFFER_SIZE / 1024);
  
  // I2S
  i2s_driver_install(I2S_NUM_0, &i2s_config, 0, NULL);
  i2s_set_pin(I2S_NUM_0, &pin_config);
  
  // WiFi
  WiFi.begin(ssid, password);
  updateDisplay("WiFi...");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.println("\nWiFi connesso: " + WiFi.localIP().toString());
  updateDisplay("WiFi OK", WiFi.localIP().toString().c_str());
  delay(1000);
  
  Serial.println("=== Sistema VAD pronto ===");
  updateDisplay("In ascolto...", "Parla ora");
}

// --- LOOP PRINCIPALE ---
void loop() {
  // Legge un frame di audio (VAD_FRAME_SAMPLES)
  int16_t frame[VAD_FRAME_SAMPLES];
  size_t bytesRead;
  i2s_read(I2S_NUM_0, frame, sizeof(frame), &bytesRead, portMAX_DELAY);
  
  if (bytesRead != sizeof(frame)) return;  // Skip se lettura incompleta
  
  // Scrivi nel circular buffer (gestisce wrap)
  size_t totalCbSamples = CIRCULAR_BUFFER_SIZE / sizeof(int16_t);
  for (size_t i = 0; i < VAD_FRAME_SAMPLES; i++) {
    circularBuffer[cbWritePos] = frame[i];
    cbWritePos = (cbWritePos + 1) % totalCbSamples;
    if (cbTotalSamples < totalCbSamples) cbTotalSamples++;
  }
  
  // Calcola RMS del frame corrente
  int16_t rms = calculateRMS(frame, VAD_FRAME_SAMPLES);
  
  // === MACCHINA A STATI VAD ===
  switch (currentState) {
    
    case VAD_IDLE:
      // Calibrazione iniziale: media del rumore ambientale
      calibrationSum += rms;
      calibrationFrames++;
      if (calibrationFrames >= 20) {  // 1 secondo di calibrazione
        // Soglia dinamica: media rumore * 1.5, minimo assoluto
        int16_t avgNoise = calibrationSum / calibrationFrames;
        SILENCE_THRESHOLD = max(400, avgNoise * 3 / 2);
        SPEECH_THRESHOLD = max(1000, avgNoise * 2);
        Serial.printf("Calibrazione: noise=%d, silence=%d, speech=%d\n", 
                      avgNoise, SILENCE_THRESHOLD, SPEECH_THRESHOLD);
        calibrationFrames = 0;
        calibrationSum = 0;
      }
      
      if (rms > SPEECH_THRESHOLD) {
        consecutiveSpeechFrames++;
        if (consecutiveSpeechFrames >= MIN_SPEECH_FRAMES) {
          // Iniziato parlato!
          currentState = VAD_RECORDING;
          // Posizione start = PRE_ROLL_FRAMES prima di ora
          speechStartPos = (cbWritePos + totalCbSamples - PRE_ROLL_FRAMES * VAD_FRAME_SAMPLES) % totalCbSamples;
          recordedSamples = 0;
          consecutiveSilenceFrames = 0;
          Serial.println(">>> PARLATO RILEVATO - Inizio registrazione");
          updateDisplay("Registrazione...", "");
        }
      } else {
        consecutiveSpeechFrames = 0;
      }
      break;
      
    case VAD_RECORDING:
      recordedSamples += VAD_FRAME_SAMPLES;
      
      if (rms < SILENCE_THRESHOLD) {
        consecutiveSilenceFrames++;
        if (consecutiveSilenceFrames >= END_OF_SPEECH_FRAMES) {
          // Fine parlato confermato
          currentState = VAD_SENDING;
          Serial.printf(">>> Fine parlato. Totale: %d campioni (%.1fs)\n", 
                        recordedSamples, (float)recordedSamples / SAMPLE_RATE);
          updateDisplay("Elaborazione...", "");
        }
      } else {
        consecutiveSilenceFrames = 0;
      }
      
      // Sicurezza: max durata
      if (recordedSamples >= MAX_RECORDING_FRAMES * VAD_FRAME_SAMPLES) {
        Serial.println(">>> Timeout max durata");
        currentState = VAD_SENDING;
        updateDisplay("Invio...", "");
      }
      break;
      
    case VAD_SENDING:
      // Estrai audio dal circular buffer
      size_t audioBytes = recordedSamples * sizeof(int16_t);
      int16_t* audioToSend = (int16_t*)heap_caps_malloc(audioBytes, MALLOC_CAP_SPIRAM);
      if (audioToSend) {
        readFromCircularBuffer(audioToSend, speechStartPos, recordedSamples);
        
        // Invia e ricevi trascrizione
        String result = sendAudioToServer(audioToSend, recordedSamples);
        Serial.println("Risultato: " + result);
        updateDisplay("Risultato:", result.c_str());
        
        heap_caps_free(audioToSend);  // Libera immediatamente
      } else {
        Serial.println("Errore malloc audioToSend");
        updateDisplay("Err: memoria", "");
      }
      
      // Reset per nuovo ciclo
      currentState = VAD_IDLE;
      calibrationFrames = 0;
      calibrationSum = 0;
      consecutiveSpeechFrames = 0;
      consecutiveSilenceFrames = 0;
      recordedSamples = 0;
      delay(1000);  // Pausa tra una frase e l'altra
      updateDisplay("In ascolto...", "Parla ora");
      break;
  }
  
  // Debug periodico
  static uint32_t lastDebug = 0;
  if (millis() - lastDebug > 5000) {
    lastDebug = millis();
    Serial.printf("State:%d RMS:%d FreePSRAM:%dKB\n", 
                  currentState, rms, heap_caps_get_free_size(MALLOC_CAP_SPIRAM) / 1024);
  }
}