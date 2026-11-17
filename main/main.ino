#include <Arduino.h>
#include <WiFi.h>
#include <ESPmDNS.h>
#include <WiFiUdp.h>
#include <ArduinoOTA.h>
#include <SPI.h>
#include <SD.h>
#include <FS.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <AsyncTCP.h>
#include <ESPAsyncWebServer.h>
#include <time.h>
#include <math.h>
#include "mbedtls/aes.h"
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "interface.h"

#define CSV_CABECALHO "\xEF\xBB\xBF" "Data/Hora;Tipo;Mensagem\r\n"

//wifi padrão
char ssidCorrente[32]  = "NOME DA REDE";
char senhaCorrente[32] = "SENHA DA REDE";

//pinos
#define PINO_REED_SWITCH  4
#define PINO_SD_CS        5
#define PINO_GPS_RX       16
#define PINO_GPS_TX       17
#define PINO_SOLENOIDE    26
#define PINO_SIRENE       27
#define PINO_LED_VERDE    32
#define PINO_LED_VERMELHO 33

//polaridade relé
#define SOLENOIDE_LIGADA     LOW
#define SOLENOIDE_DESLIGADA  HIGH
#define SIRENE_LIGADA        LOW
#define SIRENE_DESLIGADA     HIGH

//senha AES-256 e assinatura
const unsigned char CHAVE_AES[32] = {
    'C','H','A','V','E','_','S','E','C','R','E','T','A','_','D','O',
    '_','C','O','F','R','E','_','E','S','P','3','2','_','2','5','6'
};
const char* ASSINATURA_ESPERADA = "COFRE_TOKEN_OK!!";

// Estados da Maquina de Estados Finitos (FSM)
enum EstadoCofre {
    ESTADO_AGUARDANDO_FECHAR_INICIAL = 0,
    ESTADO_TRANCADO_VIGIANDO         = 1,
    ESTADO_DESTRAVANDO_PORTA         = 2,
    ESTADO_PORTA_ABERTA_EM_USO       = 3,
    ESTADO_ACESSO_NEGADO_AVISO       = 4,
    ESTADO_ALARME_DISPARADO          = 5
};

EstadoCofre estadoAtual = ESTADO_AGUARDANDO_FECHAR_INICIAL;

// Estrutura alinhada em 32 bits para os parametros configuraveis via Web (NVS)
struct __attribute__((aligned(4))) ConfigSistema {
    uint32_t freqLeituraSDMs;      // Padrao: 1000 ms
    uint32_t freqCicloFsmMs;       // Padrao: 20 ms
    uint32_t freqHistoricoMs;      // Padrao: 2000 ms
    uint8_t  maxTentativas;        // Padrao: 3 tentativas
    uint32_t tempoSolenoideMs;     // Padrao: 4000 ms (Max 5000 ms por seguranca termica)
    uint32_t tempoAvisoNegadoMs;   // Padrao: 5000 ms
    bool     aceitarQualquerToken; // Padrao: true
};

ConfigSistema cfg = {1000, 20, 2000, 3, 4000, 5000, true};

AsyncWebServer server(80);
WiFiServer telnetServer(23);
WiFiClient telnetClient;
Preferences preferences;

// Cache em RAM alinhado em 4 bytes + Persistencia de 1000 logs na Flash (LittleFS)
struct __attribute__((aligned(4))) RegistroLog {
    char timestamp[24];
    char tipo[8];
    char mensagem[96];
};

#define MAX_LOGS_RAM   60
#define MAX_LOGS_FLASH 1000
RegistroLog bufferLogs[MAX_LOGS_RAM];
int indiceLogHead = 0;
int totalLogsRam = 0;
int totalLogsFlash = 0;
uint32_t bootCount = 0;
bool littleFsMontado = false;

// Historico em Tempo Real (Segundos - ultimas 30 amostras reais)
#define TAM_HISTORICO 30
unsigned long historicoUptime[TAM_HISTORICO];
int historicoHeap[TAM_HISTORICO];
int historicoRssi[TAM_HISTORICO];
float historicoTemp[TAM_HISTORICO];
int indiceHistorico = 0;
bool historicoCheio = false;
unsigned long ultimaAmostraHistorico = 0;

// Historico das Ultimas 24 Horas (Apenas medicoes reais; slots nao medidos = valido false -> null no JSON)
#define TAM_HIST_24H 24
bool hist24hValido[TAM_HIST_24H];
int hist24hHeap[TAM_HIST_24H];
int hist24hRssi[TAM_HIST_24H];
float hist24hTemp[TAM_HIST_24H];
int hist24hEventos[TAM_HIST_24H];
unsigned long ultimaAmostra24h = 0;
const int indice24h = 23; // Posicao 23 = Hora atual ("0h")

// Medicao isolada e real das 5 funcoes principais (em microssegundos)
volatile unsigned long tempoExecucaoFSM    = 0;
volatile unsigned long tempoExecucaoSD     = 0;
volatile unsigned long tempoExecucaoGPS    = 0;
volatile unsigned long tempoExecucaoWeb    = 0;
volatile unsigned long tempoExecucaoTelnet = 0;
volatile float estimativaCargaTasksPct     = 0.0f;

// Variaveis operacionais e de sensores
int tentativasErradas = 0;
bool cartaoInseridoNoSlot = false;
bool pulsoDesbloqueioAtivo = false;
int ultimoStatusSD = 0; // 0 = Vazio, 1 = Valido, -1 = Invalido
uint32_t totalAberturasPorta = 0;
uint32_t totalLeiturasSD = 0;
float ultimaTempCpu = 0.0f;

unsigned long tempoInicioEstado = 0;
unsigned long tempoPortaAbriu = 0;
unsigned long tempoPortaFechou = 0;
unsigned long ultimoPiscaLed = 0;
unsigned long ultimaChecagemSD = 0;
bool estadoPiscaVermelho = false;

bool gpsFixValido = false;
double latitudeAtual  = -25.4284; // Coordenada base PUCPR Curitiba quando sem fix
double longitudeAtual = -49.2733;
unsigned long totalBytesGps = 0;
String bufferLinhaGps = "";

SemaphoreHandle_t mutexSistema = NULL;
TaskHandle_t TaskCofreHandle = NULL;
TaskHandle_t TaskRedeHandle = NULL;

float lerTemperaturaSegura() {
    float t = temperatureRead();
    if (isnan(t) || t < -10.0f || t > 120.0f) {
        return 0.0f;
    }
    return t;
}

// Obtem data/hora via RTC/NTP ou fallback para Uptime com contador de Boot
void obterTimestampAtual(char* buffer, size_t tamanho) {
    struct tm timeinfo;
    if (getLocalTime(&timeinfo, 10)) {
        strftime(buffer, tamanho, "%d/%m/%Y %H:%M:%S", &timeinfo);
    } else {
        snprintf(buffer, tamanho, "Boot#%lu - %lus", (unsigned long)bootCount, millis() / 1000);
    }
}

// Controla o limite circular de 1000 registros no arquivo /logs.csv na Flash
void apararLogsFlashSeNecessario() {
    if (!littleFsMontado || totalLogsFlash <= MAX_LOGS_FLASH) return;

    File arqOrig = LittleFS.open("/logs.csv", "r");
    if (!arqOrig) return;

    File arqTemp = LittleFS.open("/logs_tmp.csv", "w");
    if (!arqTemp) {
        arqOrig.close();
        return;
    }

    arqTemp.print(CSV_CABECALHO);

    int linhasParaPular = (totalLogsFlash - MAX_LOGS_FLASH) + 1; // +1 do cabecalho original
    int linhaAtual = 0;
    int novasLinhas = 0;

    while (arqOrig.available()) {
        String linha = arqOrig.readStringUntil('\n');
        linha.trim();
        if (linha.length() == 0) continue;
        linhaAtual++;
        if (linhaAtual > linhasParaPular) {
            arqTemp.println(linha);
            novasLinhas++;
        }
    }

    arqOrig.close();
    arqTemp.close();

    LittleFS.remove("/logs.csv");
    LittleFS.rename("/logs_tmp.csv", "/logs.csv");
    totalLogsFlash = novasLinhas;
}

// Carrega os ultimos logs da Flash para a RAM durante o boot
void carregarLogsDaFlashNoBoot() {
    if (!littleFsMontado) return;

    if (!LittleFS.exists("/logs.csv")) {
        File arq = LittleFS.open("/logs.csv", "w");
        if (arq) {
            arq.print(CSV_CABECALHO);
            arq.close();
        }
        totalLogsFlash = 0;
        return;
    }

    File arq = LittleFS.open("/logs.csv", "r");
    if (!arq) return;

    totalLogsFlash = 0;
    bool primeiraLinha = true;

    while (arq.available()) {
        String linha = arq.readStringUntil('\n');
        linha.trim();
        if (linha.length() == 0) continue;

        if (primeiraLinha && (linha.indexOf("Data/Hora;Tipo;Mensagem") != -1 || linha.indexOf("ta/Hora;Tipo;Mensagem") != -1)) {
            primeiraLinha = false;
            continue;
        }
        primeiraLinha = false;
        totalLogsFlash++;

        int p1 = linha.indexOf(';');
        int p2 = linha.indexOf(';', p1 + 1);
        if (p1 > 0 && p2 > p1) {
            String ts  = linha.substring(0, p1);
            String tp  = linha.substring(p1 + 1, p2);
            String msg = linha.substring(p2 + 1);

            snprintf(bufferLogs[indiceLogHead].timestamp, sizeof(bufferLogs[0].timestamp), "%s", ts.c_str());
            snprintf(bufferLogs[indiceLogHead].tipo, sizeof(bufferLogs[0].tipo), "%s", tp.c_str());
            snprintf(bufferLogs[indiceLogHead].mensagem, sizeof(bufferLogs[0].mensagem), "%s", msg.c_str());

            indiceLogHead = (indiceLogHead + 1) % MAX_LOGS_RAM;
            if (totalLogsRam < MAX_LOGS_RAM) totalLogsRam++;
        }
    }
    arq.close();
}

// Registra evento simultaneamente na RAM e no arquivo persistente da Flash (ate 1000 registros)
void registrarLog(const char* tipo, const char* msg) {
    if (xSemaphoreTake(mutexSistema, portMAX_DELAY) == pdTRUE) {
        char ts[24];
        obterTimestampAtual(ts, sizeof(ts));

        snprintf(bufferLogs[indiceLogHead].timestamp, sizeof(bufferLogs[0].timestamp), "%s", ts);
        snprintf(bufferLogs[indiceLogHead].tipo, sizeof(bufferLogs[0].tipo), "%s", tipo);
        snprintf(bufferLogs[indiceLogHead].mensagem, sizeof(bufferLogs[0].mensagem), "%s", msg);

        indiceLogHead = (indiceLogHead + 1) % MAX_LOGS_RAM;
        if (totalLogsRam < MAX_LOGS_RAM) totalLogsRam++;

        if (littleFsMontado) {
            File arq = LittleFS.open("/logs.csv", "a");
            if (arq) {
                arq.printf("%s;%s;%s\r\n", ts, tipo, msg);
                arq.close();
                totalLogsFlash++;
            }
            if (totalLogsFlash > MAX_LOGS_FLASH + 20) {
                apararLogsFlashSeNecessario();
            }
        }
        xSemaphoreGive(mutexSistema);
    }
}

void logMonitor(const String &msg) {
    Serial.print(msg);
    if (telnetClient && telnetClient.connected()) {
        telnetClient.print(msg);
    }
}

void logMonitorLn(const String &msg) {
    logMonitor(msg + "\r\n");
}

double converterNmeaParaDecimal(String valorBruto, String direcao) {
    if (valorBruto.length() < 4) return 0.0;
    int posicaoPonto = valorBruto.indexOf('.');
    int tamanhoGraus = posicaoPonto - 2;
    double graus = valorBruto.substring(0, tamanhoGraus).toDouble();
    double minutos = valorBruto.substring(tamanhoGraus).toDouble();
    double decimal = graus + (minutos / 60.0);
    if (direcao == "S" || direcao == "W") decimal *= -1.0;
    return decimal;
}

void processarSentencaRMC(String linha) {
    int indicesVirgula[12];
    int contadorVirgulas = 0;
    for (int i = 0; i < (int)linha.length() && contadorVirgulas < 12; i++) {
        if (linha[i] == ',') indicesVirgula[contadorVirgulas++] = i;
    }
    if (contadorVirgulas < 7) return;

    String statusValidade = linha.substring(indicesVirgula[1] + 1, indicesVirgula[2]);
    if (statusValidade == "A") {
        String latBruta = linha.substring(indicesVirgula[2] + 1, indicesVirgula[3]);
        String latDir   = linha.substring(indicesVirgula[3] + 1, indicesVirgula[4]);
        String lngBruta = linha.substring(indicesVirgula[4] + 1, indicesVirgula[5]);
        String lngDir   = linha.substring(indicesVirgula[5] + 1, indicesVirgula[6]);
        if (latBruta.length() > 0 && lngBruta.length() > 0) {
            latitudeAtual  = converterNmeaParaDecimal(latBruta, latDir);
            longitudeAtual = converterNmeaParaDecimal(lngBruta, lngDir);
            gpsFixValido   = true;
        }
    } else {
        gpsFixValido = false;
    }
}

// Funcao Principal 3: Parser NMEA GPS via UART2
void lerSerialGPS() {
    unsigned long inicio = micros();
    while (Serial2.available() > 0) {
        char c = (char)Serial2.read();
        totalBytesGps++;
        if (c == '\n') {
            bufferLinhaGps.trim();
            if (bufferLinhaGps.startsWith("$GPRMC") || bufferLinhaGps.startsWith("$GNRMC")) {
                processarSentencaRMC(bufferLinhaGps);
            }
            bufferLinhaGps = "";
        } else if (c != '\r' && bufferLinhaGps.length() < 120) {
            bufferLinhaGps += c;
        }
    }
    tempoExecucaoGPS = micros() - inicio;
}

// Funcao Principal 5: Gerenciador de Conexoes do Servidor Telnet (Porta 23)
void gerenciarTelnet() {
    unsigned long inicio = micros();
    if (telnetServer.hasClient()) {
        if (!telnetClient || !telnetClient.connected()) {
            if (telnetClient) telnetClient.stop();
            telnetClient = telnetServer.available();
        } else {
            telnetServer.available().stop();
        }
    }
    tempoExecucaoTelnet = micros() - inicio;
}

String obterTextoCoordenadas() {
    if (gpsFixValido) return "Lat: " + String(latitudeAtual, 6) + " | Lng: " + String(longitudeAtual, 6);
    return "Sem Fix (Base Curitiba) | Bytes: " + String(totalBytesGps);
}

bool validarTokenAES(File &arquivo) {
    if (cfg.aceitarQualquerToken && arquivo.size() > 0) return true;
    if (arquivo.size() < 32) return false;
    unsigned char iv[16], cifrado[16], decifrado[17];
    memset(decifrado, 0, sizeof(decifrado));
    arquivo.read(iv, 16);
    arquivo.read(cifrado, 16);
    mbedtls_aes_context aes;
    mbedtls_aes_init(&aes);
    mbedtls_aes_setkey_dec(&aes, CHAVE_AES, 256);
    mbedtls_aes_crypt_cbc(&aes, MBEDTLS_AES_DECRYPT, 16, iv, cifrado, decifrado);
    mbedtls_aes_free(&aes);
    return (memcmp(decifrado, ASSINATURA_ESPERADA, 16) == 0);
}

// Funcao Principal 2: Leitura SPI SD Card + Validacao AES-256
int lerEstadoCartaoSD() {
    unsigned long inicio = micros();
    SD.end();
    if (!SD.begin(PINO_SD_CS, SPI, 4000000)) {
        tempoExecucaoSD = micros() - inicio;
        ultimoStatusSD = 0;
        return 0;
    }
    totalLeiturasSD++;
    if (!SD.exists("/token.bin")) {
        SD.end();
        tempoExecucaoSD = micros() - inicio;
        ultimoStatusSD = -1;
        return -1;
    }
    File arq = SD.open("/token.bin", FILE_READ);
    if (!arq) {
        SD.end();
        tempoExecucaoSD = micros() - inicio;
        ultimoStatusSD = -1;
        return -1;
    }
    bool valido = validarTokenAES(arq);
    arq.close();
    SD.end();
    tempoExecucaoSD = micros() - inicio;
    ultimoStatusSD = valido ? 1 : -1;
    return ultimoStatusSD;
}

const char* obterNomeEstado(EstadoCofre est) {
    switch (est) {
        case ESTADO_AGUARDANDO_FECHAR_INICIAL: return "0 - Aguardando Fechar Porta";
        case ESTADO_TRANCADO_VIGIANDO:         return "1 - Trancado e Vigiando";
        case ESTADO_DESTRAVANDO_PORTA:         return "2A - Destravando Solenoide";
        case ESTADO_PORTA_ABERTA_EM_USO:       return "2B - Porta Aberta em Uso";
        case ESTADO_ACESSO_NEGADO_AVISO:       return "3 - Acesso Negado (Aviso)";
        case ESTADO_ALARME_DISPARADO:          return "4 - ALARME DISPARADO";
        default:                               return "Desconhecido";
    }
}

void entrarEstadoTrancado() {
    estadoAtual = ESTADO_TRANCADO_VIGIANDO;
    pulsoDesbloqueioAtivo = false;
    digitalWrite(PINO_SOLENOIDE, SOLENOIDE_DESLIGADA);
    digitalWrite(PINO_SIRENE, SIRENE_DESLIGADA);
    digitalWrite(PINO_LED_VERDE, LOW);
    digitalWrite(PINO_LED_VERMELHO, LOW);
    registrarLog("INFO", "Cofre trancado e vigiando.");
    logMonitorLn(">>> [ESTADO 1] COFRE TRANCADO E VIGIANDO <<<");
}

void entrarEstadoAutorizado() {
    estadoAtual = ESTADO_DESTRAVANDO_PORTA;
    tempoInicioEstado = millis();
    tempoPortaAbriu = 0;
    tentativasErradas = 0;
    pulsoDesbloqueioAtivo = true;
    digitalWrite(PINO_SIRENE, SIRENE_DESLIGADA);
    digitalWrite(PINO_LED_VERMELHO, LOW);
    digitalWrite(PINO_LED_VERDE, HIGH);
    digitalWrite(PINO_SOLENOIDE, SOLENOIDE_LIGADA);
    registrarLog("INFO", "Acesso autorizado via cartao SD.");
    logMonitorLn(">>> [ESTADO 2A] ACESSO AUTORIZADO! Solenoide acionada <<<");
}

void entrarEstadoAcessoNegado() {
    estadoAtual = ESTADO_ACESSO_NEGADO_AVISO;
    tempoInicioEstado = millis();
    ultimoPiscaLed = millis();
    estadoPiscaVermelho = true;
    digitalWrite(PINO_SOLENOIDE, SOLENOIDE_DESLIGADA);
    digitalWrite(PINO_SIRENE, SIRENE_DESLIGADA);
    digitalWrite(PINO_LED_VERDE, LOW);
    digitalWrite(PINO_LED_VERMELHO, HIGH);
    registrarLog("WARN", "Tentativa com cartao SD invalido.");
    logMonitorLn(">>> [ESTADO 3] CARTAO INVALIDO! Aviso visual ativado <<<");
}

void entrarEstadoAlarme(const char* motivo) {
    estadoAtual = ESTADO_ALARME_DISPARADO;
    tempoInicioEstado = millis();
    ultimoPiscaLed = millis();
    digitalWrite(PINO_SOLENOIDE, SOLENOIDE_DESLIGADA);
    digitalWrite(PINO_LED_VERDE, LOW);
    digitalWrite(PINO_SIRENE, SIRENE_LIGADA);
    registrarLog("ERROR", motivo);
    logMonitorLn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
    logMonitorLn(">>> [ALARME] " + String(motivo) + " | GPS: " + obterTextoCoordenadas());
    logMonitorLn("!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!!");
}

// Inicializa os vetores de 24h sem dados inventados (apenas a hora atual recebe leitura real)
void inicializarVetores24h() {
    for (int i = 0; i < TAM_HIST_24H - 1; i++) {
        hist24hValido[i]  = false;
        hist24hHeap[i]    = 0;
        hist24hRssi[i]    = 0;
        hist24hTemp[i]    = 0.0f;
        hist24hEventos[i] = 0;
    }
    hist24hValido[indice24h]  = true;
    hist24hHeap[indice24h]    = ESP.getFreeHeap();
    hist24hRssi[indice24h]    = (WiFi.status() == WL_CONNECTED) ? WiFi.RSSI() : -100;
    hist24hTemp[indice24h]    = lerTemperaturaSegura();
    hist24hEventos[indice24h] = 0;
}

// -------------------------------------------------------------------------
// TASK 1: Maquina de Estados Finitos do Cofre e Sensores (Core 1) - Stack 12288
// -------------------------------------------------------------------------
void TaskCofreCode(void * pvParameters) {
    static uint8_t contAberta = 255; // Inicia em 255 assumindo porta aberta no boot
    static bool portaEstavaFechada = false;

    for (;;) {
        unsigned long inicioFSM = micros();
        unsigned long descontoSDUs = 0;

        bool leituraPino = (digitalRead(PINO_REED_SWITCH) == HIGH);
        contAberta = leituraPino ? min((int)contAberta + 1, 255) : 0;
        bool portaFechada = (contAberta < 3);

        if (!portaFechada && portaEstavaFechada) {
            totalAberturasPorta++;
            hist24hEventos[indice24h]++;
        }
        portaEstavaFechada = portaFechada;

        unsigned long agoras = millis();

        switch (estadoAtual) {
            case ESTADO_AGUARDANDO_FECHAR_INICIAL:
                if (agoras - ultimoPiscaLed >= 300) {
                    ultimoPiscaLed = agoras;
                    estadoPiscaVermelho = !estadoPiscaVermelho;
                    digitalWrite(PINO_LED_VERMELHO, estadoPiscaVermelho ? HIGH : LOW);
                }
                if (portaFechada) {
                    digitalWrite(PINO_LED_VERMELHO, LOW);
                    entrarEstadoTrancado();
                }
                break;

            case ESTADO_TRANCADO_VIGIANDO:
                if (!portaFechada) {
                    entrarEstadoAlarme("VIOLACAO FISICA DA PORTA (ARROMBAMENTO)");
                    break;
                }
                if (agoras - ultimaChecagemSD >= cfg.freqLeituraSDMs) {
                    ultimaChecagemSD = agoras;
                    int resultadoSD = lerEstadoCartaoSD();
                    descontoSDUs = tempoExecucaoSD;
                    if (resultadoSD == 0) {
                        cartaoInseridoNoSlot = false;
                    } else if (!cartaoInseridoNoSlot) {
                        cartaoInseridoNoSlot = true;
                        hist24hEventos[indice24h]++;
                        if (resultadoSD == 1) {
                            entrarEstadoAutorizado();
                        } else {
                            tentativasErradas++;
                            if (tentativasErradas >= (int)cfg.maxTentativas) {
                                entrarEstadoAlarme("BLOQUEIO: LIMITE DE TENTATIVAS INVALIDAS");
                            } else {
                                entrarEstadoAcessoNegado();
                            }
                        }
                    }
                }
                break;

            case ESTADO_DESTRAVANDO_PORTA:
                if (!portaFechada) {
                    if (tempoPortaAbriu == 0) {
                        tempoPortaAbriu = agoras;
                    } else if (agoras - tempoPortaAbriu >= 500) {
                        digitalWrite(PINO_SOLENOIDE, SOLENOIDE_DESLIGADA);
                        pulsoDesbloqueioAtivo = false;
                        estadoAtual = ESTADO_PORTA_ABERTA_EM_USO;
                        tempoPortaFechou = 0;
                        registrarLog("INFO", "Porta aberta pelo usuario.");
                    }
                } else if (agoras - tempoInicioEstado >= cfg.tempoSolenoideMs) {
                    entrarEstadoTrancado();
                }
                break;

            case ESTADO_PORTA_ABERTA_EM_USO:
                if (agoras - ultimaChecagemSD >= cfg.freqLeituraSDMs) {
                    ultimaChecagemSD = agoras;
                    if (lerEstadoCartaoSD() == 0) cartaoInseridoNoSlot = false;
                    descontoSDUs = tempoExecucaoSD;
                }
                if (portaFechada) {
                    if (tempoPortaFechou == 0) tempoPortaFechou = agoras;
                    else if (agoras - tempoPortaFechou >= 1000) entrarEstadoTrancado();
                } else {
                    tempoPortaFechou = 0;
                }
                break;

            case ESTADO_ACESSO_NEGADO_AVISO:
                if (agoras - ultimoPiscaLed >= 100) {
                    ultimoPiscaLed = agoras;
                    estadoPiscaVermelho = !estadoPiscaVermelho;
                    digitalWrite(PINO_LED_VERMELHO, estadoPiscaVermelho ? HIGH : LOW);
                }
                if (!portaFechada) {
                    entrarEstadoAlarme("VIOLACAO DURANTE AVISO DE ERRO");
                    break;
                }
                if (agoras - tempoInicioEstado >= cfg.tempoAvisoNegadoMs) {
                    digitalWrite(PINO_LED_VERMELHO, LOW);
                    entrarEstadoTrancado();
                }
                break;

            case ESTADO_ALARME_DISPARADO:
                if (agoras - ultimoPiscaLed >= 150) {
                    ultimoPiscaLed = agoras;
                    estadoPiscaVermelho = !estadoPiscaVermelho;
                    digitalWrite(PINO_LED_VERMELHO, estadoPiscaVermelho ? HIGH : LOW);
                }
                if (agoras - ultimaChecagemSD >= cfg.freqLeituraSDMs) {
                    ultimaChecagemSD = agoras;
                    int resultadoSD = lerEstadoCartaoSD();
                    descontoSDUs = tempoExecucaoSD;
                    if (resultadoSD == 1) {
                        cartaoInseridoNoSlot = true;
                        entrarEstadoAutorizado();
                    } else if (resultadoSD == 0) {
                        cartaoInseridoNoSlot = false;
                    }
                }
                break;
        }

        // Coleta em tempo real (em segundos) para os graficos dinamicos
        if (agoras - ultimaAmostraHistorico >= cfg.freqHistoricoMs) {
            ultimaAmostraHistorico = agoras;
            ultimaTempCpu = lerTemperaturaSegura();

            historicoUptime[indiceHistorico] = agoras / 1000;
            historicoHeap[indiceHistorico]   = ESP.getFreeHeap();
            historicoRssi[indiceHistorico]   = WiFi.RSSI();
            historicoTemp[indiceHistorico]   = ultimaTempCpu;

            // Atualiza o slot da hora atual ("0h") do consolidado de 24h com medicoes reais
            hist24hValido[indice24h] = true;
            hist24hHeap[indice24h]   = ESP.getFreeHeap();
            hist24hRssi[indice24h]   = WiFi.RSSI();
            hist24hTemp[indice24h]   = ultimaTempCpu;

            indiceHistorico = (indiceHistorico + 1) % TAM_HISTORICO;
            if (indiceHistorico == 0) historicoCheio = true;
        }

        // Rotaciona janela de 24h a cada 1 hora real (3600000 ms)
        if (agoras - ultimaAmostra24h >= 3600000UL) {
            ultimaAmostra24h = agoras;
            for (int i = 0; i < TAM_HIST_24H - 1; i++) {
                hist24hValido[i]  = hist24hValido[i + 1];
                hist24hHeap[i]    = hist24hHeap[i + 1];
                hist24hRssi[i]    = hist24hRssi[i + 1];
                hist24hTemp[i]    = hist24hTemp[i + 1];
                hist24hEventos[i] = hist24hEventos[i + 1];
            }
            hist24hValido[indice24h]  = true;
            hist24hEventos[indice24h] = 0;
        }

        // Funcao Principal 1: Mede estritamente a FSM (descontando o tempo SPI SD quando executado)
        unsigned long duracaoTotalLoop = micros() - inicioFSM;
        tempoExecucaoFSM = (duracaoTotalLoop > descontoSDUs) ? (duracaoTotalLoop - descontoSDUs) : duracaoTotalLoop;

        // Taxa de ocupacao real medida das tasks da aplicacao (sem offsets artificiais)
        unsigned long tempoAtivoUs = tempoExecucaoFSM + tempoExecucaoWeb + tempoExecucaoTelnet + tempoExecucaoGPS;
        unsigned long janelaUs = cfg.freqCicloFsmMs * 1000UL;
        float cargaInstantanea = ((float)tempoAtivoUs / (float)(janelaUs > 0 ? janelaUs : 20000)) * 100.0f;
        if (cargaInstantanea > 100.0f) cargaInstantanea = 100.0f;
        estimativaCargaTasksPct = (estimativaCargaTasksPct * 0.8f) + (cargaInstantanea * 0.2f);

        vTaskDelay(cfg.freqCicloFsmMs / portTICK_PERIOD_MS);
    }
}

// -------------------------------------------------------------------------
// TASK 2: Rede, OTA, Telnet e GPS (Core 0) - Stack 8192
// -------------------------------------------------------------------------
void TaskRedeCode(void * pvParameters) {
    for (;;) {
        // Funcao Principal 4: Servico ArduinoOTA
        unsigned long inicioWeb = micros();
        ArduinoOTA.handle();
        tempoExecucaoWeb = micros() - inicioWeb;

        // Funcao Principal 5: Gerenciador Telnet
        gerenciarTelnet();

        // Funcao Principal 3: Leitura UART2 GPS
        lerSerialGPS();

        vTaskDelay(50 / portTICK_PERIOD_MS);
    }
}

int calcularQualidadeWifi(int rssi) {
    if (rssi <= -100) return 0;
    if (rssi >= -50) return 100;
    return 2 * (rssi + 100);
}

void carregarConfiguracoesNVS() {
    preferences.begin("cofre-cfg", false);
    String sSsid  = preferences.getString("ssid", ssidCorrente);
    String sSenha = preferences.getString("senha", senhaCorrente);
    sSsid.toCharArray(ssidCorrente, sizeof(ssidCorrente));
    sSenha.toCharArray(senhaCorrente, sizeof(senhaCorrente));

    cfg.freqLeituraSDMs      = preferences.getUInt("f_sd", 1000);
    cfg.freqCicloFsmMs       = preferences.getUInt("f_fsm", 20);
    cfg.freqHistoricoMs      = preferences.getUInt("f_hist", 2000);
    cfg.maxTentativas        = preferences.getUChar("m_tent", 3);
    cfg.tempoSolenoideMs     = constrain(preferences.getUInt("t_sol", 4000), 1000, 5000);
    cfg.tempoAvisoNegadoMs   = preferences.getUInt("t_avi", 5000);
    cfg.aceitarQualquerToken = preferences.getBool("q_tok", true);

    bootCount = preferences.getUInt("boots", 0) + 1;
    preferences.putUInt("boots", bootCount);
}

void setup() {
    WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
    Serial.begin(115200);

    mutexSistema = xSemaphoreCreateMutex();

    // Inicializa pinos de potencia em estado seguro (Active LOW = HIGH desligado)
    digitalWrite(PINO_SOLENOIDE, SOLENOIDE_DESLIGADA);
    digitalWrite(PINO_SIRENE, SIRENE_DESLIGADA);
    pinMode(PINO_SOLENOIDE, OUTPUT);
    pinMode(PINO_SIRENE, OUTPUT);

    pinMode(PINO_LED_VERDE, OUTPUT);
    pinMode(PINO_LED_VERMELHO, OUTPUT);
    digitalWrite(PINO_LED_VERDE, LOW);
    digitalWrite(PINO_LED_VERMELHO, LOW);

    pinMode(PINO_REED_SWITCH, INPUT_PULLUP);

    // Inicializa Flash interna LittleFS para persistencia dos 1000 logs
    if (LittleFS.begin(true)) {
        littleFsMontado = true;
        carregarLogsDaFlashNoBoot();
    }

    SPI.begin(18, 19, 23, PINO_SD_CS);
    carregarConfiguracoesNVS();

    WiFi.mode(WIFI_STA);
    WiFi.begin(ssidCorrente, senhaCorrente);
    WiFi.setTxPower(WIFI_POWER_15dBm); // Equilibrio entre alcance na sala e corrente de pico

    // Configura o daemon SNTP independentemente do tempo de conexao do Wi-Fi
    configTime(-3 * 3600, 0, "pool.ntp.org", "time.nist.gov", "a.st1.ntp.br");

    Serial.print("[WIFI] Conectando a ");
    Serial.println(ssidCorrente);
    unsigned long inicioWiFiconn = millis();
    while (WiFi.status() != WL_CONNECTED && millis() - inicioWiFiconn < 10000) {
        delay(500);
        Serial.print(".");
    }
    Serial.println();
    if (WiFi.status() == WL_CONNECTED) {
        Serial.print("[WIFI] Conectado! Acesse o painel em: http://");
        Serial.println(WiFi.localIP());
    } else {
        Serial.println("[WIFI] Nao conectou em 10s. Tentando reconexao em background...");
    }

    ultimaTempCpu = lerTemperaturaSegura();

    // Popula primeira amostra em tempo real (t = 0s) e vetores de 24h
    historicoUptime[0] = millis() / 1000;
    historicoHeap[0]   = ESP.getFreeHeap();
    historicoRssi[0]   = WiFi.RSSI();
    historicoTemp[0]   = ultimaTempCpu;
    indiceHistorico    = 1;
    inicializarVetores24h();

    telnetServer.begin();
    telnetServer.setNoDelay(true);

    ArduinoOTA.setHostname("smart-vault");
    ArduinoOTA.begin();

    Serial2.begin(9600, SERIAL_8N1, PINO_GPS_RX, PINO_GPS_TX);

    // -------------------------------------------------------------------------
    // ROTAS DO SERVIDOR WEB ASSINCRONO
    // -------------------------------------------------------------------------
    server.on("/", HTTP_GET, [](AsyncWebServerRequest *request){
        request->send_P(200, "text/html", paginaHTML);
    });

    // Status Geral + Sensores em Tempo Real
    server.on("/api/status", HTTP_GET, [](AsyncWebServerRequest *request){
        char buf[640];
        bool portaFechada = (digitalRead(PINO_REED_SWITCH) == LOW);
        const char* txtSd = (ultimoStatusSD == 1) ? "Token Valido" : ((ultimoStatusSD == -1) ? "Token Invalido" : "Slot Vazio");
        float tempSegura = isnan(ultimaTempCpu) ? 0.0f : ultimaTempCpu;

        snprintf(buf, sizeof(buf),
            "{\"estado\":\"%s\",\"porta\":\"%s\",\"pino_reed\":%d,\"tentativas\":%d,"
            "\"max_tent\":%u,\"gps\":\"%s\",\"lat\":%.6f,\"lng\":%.6f,\"fix\":%s,"
            "\"bytes_gps\":%lu,\"sd_status\":\"%s\",\"leituras_sd\":%lu,"
            "\"aberturas\":%lu,\"temp_cpu\":%.1f}",
            obterNomeEstado(estadoAtual),
            portaFechada ? "Fechada (Segura)" : "Aberta",
            portaFechada ? 0 : 1,
            tentativasErradas,
            cfg.maxTentativas,
            obterTextoCoordenadas().c_str(),
            latitudeAtual,
            longitudeAtual,
            gpsFixValido ? "true" : "false",
            totalBytesGps,
            txtSd,
            (unsigned long)totalLeiturasSD,
            (unsigned long)totalAberturasPorta,
            tempSegura
        );
        request->send(200, "application/json", buf);
    });

    // Metricas Completas de Performance (CPU, Memorias, 5 Funcoes, Tasks, Wi-Fi e OTA)
    server.on("/api/performance", HTTP_GET, [](AsyncWebServerRequest *request){
        char buf[850];
        int rssi = WiFi.RSSI();
        uint32_t stackCofreLivre = TaskCofreHandle ? uxTaskGetStackHighWaterMark(TaskCofreHandle) : 0;
        uint32_t stackRedeLivre  = TaskRedeHandle  ? uxTaskGetStackHighWaterMark(TaskRedeHandle)  : 0;
        uint32_t totalThreads    = uxTaskGetNumberOfTasks();
        float cargaSegura        = isnan(estimativaCargaTasksPct) ? 0.0f : estimativaCargaTasksPct;

        snprintf(buf, sizeof(buf),
            "{\"cpu_load\":%.2f,\"cpu_freq\":%u,"
            "\"heap_livre\":%u,\"heap_total\":%u,\"heap_min\":%u,"
            "\"stack_cofre\":%u,\"stack_rede\":%u,"
            "\"flash_total\":%u,\"flash_usada\":%u,\"flash_livre\":%u,"
            "\"psram_total\":%u,\"psram_livre\":%u,"
            "\"num_tasks\":%u,"
            "\"wifi_status\":\"%s\",\"ssid\":\"%s\",\"ip\":\"%s\",\"gw\":\"%s\",\"mac\":\"%s\",\"bssid\":\"%s\",\"canal\":%d,\"rssi\":%d,\"qualidade\":%d,"
            "\"ota_hostname\":\"smart-vault\",\"ota_porta\":3232,"
            "\"t_fsm\":%lu,\"t_sd\":%lu,\"t_gps\":%lu,\"t_web\":%lu,\"t_telnet\":%lu}",
            cargaSegura, ESP.getCpuFreqMHz(),
            ESP.getFreeHeap(), ESP.getHeapSize(), ESP.getMinFreeHeap(),
            (unsigned int)stackCofreLivre, (unsigned int)stackRedeLivre,
            ESP.getFlashChipSize(), ESP.getSketchSize(), ESP.getFreeSketchSpace(),
            ESP.getPsramSize(), ESP.getFreePsram(),
            (unsigned int)totalThreads,
            (WiFi.status() == WL_CONNECTED) ? "Conectado" : "Desconectado",
            WiFi.SSID().c_str(),
            WiFi.localIP().toString().c_str(),
            WiFi.gatewayIP().toString().c_str(),
            WiFi.macAddress().c_str(),
            WiFi.BSSIDstr().c_str(),
            WiFi.channel(),
            rssi, calcularQualidadeWifi(rssi),
            tempoExecucaoFSM, tempoExecucaoSD, tempoExecucaoGPS, tempoExecucaoWeb, tempoExecucaoTelnet
        );
        request->send(200, "application/json", buf);
    });

    // Historico em Tempo Real (Segundos) + Consolidado das Ultimas 24 Horas (null para horas antes do boot)
    server.on("/api/historico", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        int limite = historicoCheio ? TAM_HISTORICO : indiceHistorico;

        response->print("{\"labels\":[");
        for (int i = 0; i < limite; i++) {
            int idx = historicoCheio ? (indiceHistorico + i) % TAM_HISTORICO : i;
            response->printf("\"%lus\"%s", historicoUptime[idx], (i < limite - 1) ? "," : "");
        }
        response->print("],\"heap\":[");
        for (int i = 0; i < limite; i++) {
            int idx = historicoCheio ? (indiceHistorico + i) % TAM_HISTORICO : i;
            response->printf("%d%s", historicoHeap[idx], (i < limite - 1) ? "," : "");
        }
        response->print("],\"rssi\":[");
        for (int i = 0; i < limite; i++) {
            int idx = historicoCheio ? (indiceHistorico + i) % TAM_HISTORICO : i;
            response->printf("%d%s", historicoRssi[idx], (i < limite - 1) ? "," : "");
        }
        response->print("],\"temp\":[");
        for (int i = 0; i < limite; i++) {
            int idx = historicoCheio ? (indiceHistorico + i) % TAM_HISTORICO : i;
            float t = isnan(historicoTemp[idx]) ? 0.0f : historicoTemp[idx];
            response->printf("%.1f%s", t, (i < limite - 1) ? "," : "");
        }
        response->print("],\"labels24h\":[");
        for (int i = 0; i < TAM_HIST_24H; i++) {
            response->printf("\"-%dh\"%s", (TAM_HIST_24H - 1 - i), (i < TAM_HIST_24H - 1) ? "," : "");
        }
        response->print("],\"heap24h\":[");
        for (int i = 0; i < TAM_HIST_24H; i++) {
            if (hist24hValido[i]) response->printf("%d", hist24hHeap[i]);
            else response->print("null");
            if (i < TAM_HIST_24H - 1) response->print(",");
        }
        response->print("],\"rssi24h\":[");
        for (int i = 0; i < TAM_HIST_24H; i++) {
            if (hist24hValido[i]) response->printf("%d", hist24hRssi[i]);
            else response->print("null");
            if (i < TAM_HIST_24H - 1) response->print(",");
        }
        response->print("],\"temp24h\":[");
        for (int i = 0; i < TAM_HIST_24H; i++) {
            if (hist24hValido[i]) {
                float t = isnan(hist24hTemp[i]) ? 0.0f : hist24hTemp[i];
                response->printf("%.1f", t);
            } else {
                response->print("null");
            }
            if (i < TAM_HIST_24H - 1) response->print(",");
        }
        response->print("],\"ev24h\":[");
        for (int i = 0; i < TAM_HIST_24H; i++) {
            if (hist24hValido[i]) response->printf("%d", hist24hEventos[i]);
            else response->print("null");
            if (i < TAM_HIST_24H - 1) response->print(",");
        }
        response->print("]}");
        request->send(response);
    });

    // Listagem de Logs Persistentes
    server.on("/api/logs", HTTP_GET, [](AsyncWebServerRequest *request){
        AsyncResponseStream *response = request->beginResponseStream("application/json");
        response->printf("{\"total_flash\":%d,\"max_flash\":%d,\"itens\":[", totalLogsFlash, MAX_LOGS_FLASH);
        if (xSemaphoreTake(mutexSistema, portMAX_DELAY) == pdTRUE) {
            int qtd = totalLogsRam;
            int idx = (indiceLogHead - qtd + MAX_LOGS_RAM) % MAX_LOGS_RAM;
            for (int i = 0; i < qtd; i++) {
                int pos = (idx + i) % MAX_LOGS_RAM;
                response->printf("{\"time\":\"%s\",\"tipo\":\"%s\",\"msg\":\"%s\"}%s",
                    bufferLogs[pos].timestamp,
                    bufferLogs[pos].tipo,
                    bufferLogs[pos].mensagem,
                    (i < qtd - 1) ? "," : ""
                );
            }
            xSemaphoreGive(mutexSistema);
        }
        response->print("]}");
        request->send(response);
    });

    // Exportacao de Logs Persistentes da Flash direto para Excel (.CSV) sem duplicar Content-Disposition
    server.on("/api/logs/export", HTTP_GET, [](AsyncWebServerRequest *request){
        if (littleFsMontado && LittleFS.exists("/logs.csv")) {
            request->send(LittleFS, "/logs.csv", "text/csv; charset=utf-8", true);
        } else {
            request->send(404, "text/plain", "Arquivo de logs nao encontrado na Flash.");
        }
    });

    // Limpeza dos Logs na Flash e RAM (regrava o cabecalho UTF-8 BOM limpo)
    server.on("/api/logs/clear", HTTP_GET, [](AsyncWebServerRequest *request){
        if (xSemaphoreTake(mutexSistema, portMAX_DELAY) == pdTRUE) {
            totalLogsRam = 0;
            indiceLogHead = 0;
            totalLogsFlash = 0;
            if (littleFsMontado) {
                File arq = LittleFS.open("/logs.csv", "w");
                if (arq) {
                    arq.print(CSV_CABECALHO);
                    arq.close();
                }
            }
            xSemaphoreGive(mutexSistema);
        }
        registrarLog("INFO", "Historico de logs reiniciado pelo usuario.");
        request->send(200, "text/plain", "Logs apagados e cabecalho CSV recriado com sucesso!");
    });

    // Leitura dos Parametros de Configuracao Atuais
    server.on("/api/config/get", HTTP_GET, [](AsyncWebServerRequest *request){
        char buf[300];
        snprintf(buf, sizeof(buf),
            "{\"f_sd\":%u,\"f_fsm\":%u,\"f_hist\":%u,\"m_tent\":%u,\"t_sol\":%u,\"t_avi\":%u,\"q_tok\":%s,\"ssid\":\"%s\"}",
            cfg.freqLeituraSDMs, cfg.freqCicloFsmMs, cfg.freqHistoricoMs,
            cfg.maxTentativas, cfg.tempoSolenoideMs, cfg.tempoAvisoNegadoMs,
            cfg.aceitarQualquerToken ? "true" : "false",
            ssidCorrente
        );
        request->send(200, "application/json", buf);
    });

    // Salvamento de Frequencia dos Sensores e Parametros Operacionais (NVS sem reiniciar)
    server.on("/api/config/parametros", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("f_sd"))   cfg.freqLeituraSDMs      = constrain(request->getParam("f_sd")->value().toInt(), 500, 10000);
        if (request->hasParam("f_fsm"))  cfg.freqCicloFsmMs       = constrain(request->getParam("f_fsm")->value().toInt(), 10, 200);
        if (request->hasParam("f_hist")) cfg.freqHistoricoMs      = constrain(request->getParam("f_hist")->value().toInt(), 1000, 60000);
        if (request->hasParam("m_tent")) cfg.maxTentativas        = constrain(request->getParam("m_tent")->value().toInt(), 1, 10);
        if (request->hasParam("t_sol"))  cfg.tempoSolenoideMs     = constrain(request->getParam("t_sol")->value().toInt(), 1000, 5000);
        if (request->hasParam("t_avi"))  cfg.tempoAvisoNegadoMs   = constrain(request->getParam("t_avi")->value().toInt(), 1000, 20000);
        if (request->hasParam("q_tok"))  cfg.aceitarQualquerToken = (request->getParam("q_tok")->value() == "1");

        preferences.putUInt("f_sd", cfg.freqLeituraSDMs);
        preferences.putUInt("f_fsm", cfg.freqCicloFsmMs);
        preferences.putUInt("f_hist", cfg.freqHistoricoMs);
        preferences.putUChar("m_tent", cfg.maxTentativas);
        preferences.putUInt("t_sol", cfg.tempoSolenoideMs);
        preferences.putUInt("t_avi", cfg.tempoAvisoNegadoMs);
        preferences.putBool("q_tok", cfg.aceitarQualquerToken);

        registrarLog("INFO", "Parametros operacionais e frequencias atualizados na Flash NVS.");
        request->send(200, "text/plain", "Parametros salvos na Flash NVS e aplicados imediatamente!");
    });

    // Troca de Ponto de Acesso Wi-Fi (NVS sem regravar firmware)
    server.on("/api/config", HTTP_GET, [](AsyncWebServerRequest *request){
        if (request->hasParam("ssid") && request->hasParam("senha")) {
            String novoSsid = request->getParam("ssid")->value();
            String novaSenha = request->getParam("senha")->value();
            preferences.putString("ssid", novoSsid);
            preferences.putString("senha", novaSenha);
            registrarLog("WARN", "Credenciais Wi-Fi alteradas via Web. Reiniciando...");
            request->send(200, "text/plain", "Novo Wi-Fi salvo na Flash! Reiniciando o Smart Vault...");
            delay(800);
            ESP.restart();
        } else {
            request->send(400, "text/plain", "Parametros invalidos.");
        }
    });

    // Gerenciamento de Energia (Light Sleep)
    server.on("/api/energiasleep", HTTP_GET, [](AsyncWebServerRequest *request){
        registrarLog("WARN", "Modo Light Sleep (10s) acionado via interface web.");
        request->send(200, "text/plain", "Entrando em Light Sleep por 10 segundos...");
        delay(100);
        esp_sleep_enable_timer_wakeup(10000000ULL);
        esp_light_sleep_start();
    });

    server.begin();

    // TaskCofre com 12288 bytes (12 KB) para acomodar VFS LittleFS + SD + mbedtls com folga
    xTaskCreatePinnedToCore(TaskCofreCode, "TaskCofre", 12288, NULL, 2, &TaskCofreHandle, 1);
    xTaskCreatePinnedToCore(TaskRedeCode,  "TaskRede",  8192,  NULL, 1, &TaskRedeHandle,  0);

    registrarLog("INFO", "Smart Vault inicializado com sucesso.");
}

void loop() {
    vTaskDelete(NULL);
}
