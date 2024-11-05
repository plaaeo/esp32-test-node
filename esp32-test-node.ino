#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <cstdint>

#include "gui/helper.h"
#include "pins_arduino.h"

const uint32_t sf[] = {7, 8, 9, 10, 11, 12};
const uint32_t cr[] = {5, 8};

const size_t sfLen = sizeof(sf) / sizeof(uint32_t);
const size_t crLen = sizeof(cr) / sizeof(uint32_t);

#define RX_WAIT_DELAY 100
#define TESTS_PER_CYCLE 5
#define POSSIBLE_PARAMETERS (sfLen * crLen)

// Definir pinos dependendo da placa
#if defined(WIFI_LoRa_32_V2)
#define Radio SX1276
#define MAX_PACKET_LENGTH RADIOLIB_SX127X_MAX_PACKET_LENGTH
#elif defined(WIFI_LoRa_32_V3)
#define Radio SX1262
#define MAX_PACKET_LENGTH RADIOLIB_SX126X_MAX_PACKET_LENGTH
#endif

#define BUTTON GPIO_NUM_0
#define LORA_NSS SS
#define LORA_DIO DIO0
#define LORA_RST RST_LoRa

// O radio SX1276 não possui pino BUSY
#ifdef BUSY_LoRa
#define LORA_BUSY BUSY_LoRa
#else
#define LORA_BUSY RADIOLIB_NC
#endif

button_t button = button_t(BUTTON);

Radio radio = new Module(LORA_NSS, LORA_DIO, LORA_RST, LORA_BUSY);

Adafruit_SSD1306 display =
    Adafruit_SSD1306(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, RST_OLED);

volatile bool didIrq = false;

// A ISR executada para qualquer evento LoRa, apenas seta a flag `didIrq`.
//
// O código que realmente lida com os interrupts é executado no `loop()`,
// pois, ao executá-lo direto na ISR, consistentemente causava crashes
// em testes.
ICACHE_RAM_ATTR void setIrqFlag(void) {
    didIrq = true;
}

void waitForIrq(void) {
    while (!didIrq) {
    };

    delay(10);
    didIrq = false;
}

DataRate_t dr;

// Conta quantos ciclos (transmitindo -> recebendo -> ...) ocorreram até agora.
uint32_t cycles = 0;
uint32_t cyclesTotal = 0;

uint32_t rxSucessos = 0;
uint32_t rxCorrompidos = 0;
uint32_t rxPerdidos = 0;

// Atualiza os parametros para o teste atual.
void updateParameters() {
    uint32_t index = cyclesTotal / TESTS_PER_CYCLE;

    dr.lora.bandwidth = 62.5;
    dr.lora.codingRate = cr[index % crLen];
    dr.lora.spreadingFactor = sf[(index / crLen) % sfLen];

    radio.setDataRate(dr);
}

uint8_t largeMessage[MAX_PACKET_LENGTH];

enum { kReceiver, kTransmitter } role = kReceiver;

void setup() {
    largeMessage[0] = '0';

    for (size_t i = 1; i < sizeof(largeMessage); i++) {
        largeMessage[i] = 'A';
    }

    Serial.begin(115200);
    SPI.begin(SCK, MISO, MOSI, SS);

    // Inicia o botão programável
    button.setup();

    // Inicializa o monitor OLED
    Wire.begin(SDA_OLED, SCL_OLED);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);

    // Inicializa o radio LoRa
    int16_t e = radio.begin(915.0, 62.5, 7, 5, 0x12, 10, 8);
    if (e != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Erro ao inicializar LoRa (cod. %d)\n", e);

        while (true) {
        };
    }

    // Define a ISR para quando uma mensagem for enviada/recebida.
    radio.setPacketReceivedAction(setIrqFlag);
    radio.setPacketSentAction(setIrqFlag);

    Serial.println("Inicializado com sucesso.");

    updateParameters();
}

uint32_t cvtTimeout(uint32_t timeout) {
#if defined(WIFI_LoRa_32_V2)
    // TODO: Testar
    timeout = 192;
#elif defined(WIFI_LoRa_32_V3)
    timeout = (timeout * 1000000) / 15625;
#endif

    return timeout;
}

// Envia um pacote e aguarda ele terminar de ser enviado.
// Retorna e imprime no `Serial` qualquer erro que ocorrer durante a
// transmissão.
int16_t sendBlocking(const uint8_t *message, uint8_t size) {
    int16_t e = radio.startTransmit(message, size);

    if (e != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Erro ao transmitir mensagem (cod. %d)\n", e);
        return e;
    }

    // Esperar o pacote terminar de ser enviado.
    waitForIrq();

    return e;
}

struct recv_result_t {
    int16_t e;
    const uint8_t *message;
    uint16_t length;
};

// Aguarda até que um pacote seja recebido, ou ocorra timeout.
// O timeout é em millisegundos.
//
// Retorna e imprime no `Serial` qualquer erro que ocorrer durante a recepção.
struct recv_result_t recvBlocking(uint32_t timeout) {
    static uint8_t message[MAX_PACKET_LENGTH];

    recv_result_t res;
    res.message = NULL;
    res.length = 0;

    timeout = cvtTimeout(timeout);
    res.e = radio.startReceive(timeout);

    if (res.e != RADIOLIB_ERR_NONE) {
        Serial.printf(
            "[ERR] Erro ao preparar para receber mensagem (cod. %d)\n", res.e);
        return res;
    }

    // Esperar o pacote terminar de ser enviado.
    waitForIrq();

    res.message = message;
    res.length = radio.getPacketLength();
    res.e = radio.readData(message, res.length);

    return res;
}

int16_t doTransmitterLoop() {
    // E1 - Tentar enviar o pacote grande repetidamente até ter um sucesso;
    //
    // ATENÇÃO: Um sucesso significa que o módulo conseguiu transmitir a
    // mensagem completa, não significa que o receptor conseguiu receber a
    // mensagem.
    int16_t e = RADIOLIB_ERR_NONE;

    Serial.println("---,+");

    // Esperamos um tempo antes de transmitir a próxima mensagem para que o
    // receptor consiga mudar de estado a tempo.
    delay(RX_WAIT_DELAY);

    do {
        if (e != RADIOLIB_ERR_NONE) {
            Serial.println("[ERR] Tentando transmissao novamente em 2s...");
            delay(10);
        }

        e = sendBlocking(largeMessage, sizeof(largeMessage));
    } while (e != RADIOLIB_ERR_NONE);

    // E2 - Receber ACK do receptor até ter um sucesso
    //
    // Similarmente, um sucesso ocorre quando o módulo consegue entrar no estado
    // de "receptor" com sucesso.
    recv_result_t recv = {0, 0, 0};

    do {
        if (recv.e != RADIOLIB_ERR_NONE) {
            Serial.println("[ERR] Tentando receber novamente em 2s...");
            delay(10);
        }

        recv = recvBlocking(3000);
    } while (recv.message == NULL && recv.e != RADIOLIB_ERR_NONE);

    switch (recv.e) {
    case RADIOLIB_ERR_NONE:
        // Nenhum erro ocorreu ao receber o pacote.
        rxSucessos++;
        Serial.printf("---,True,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;

    case RADIOLIB_ERR_CRC_MISMATCH:
        // Um pacote corrompido foi recebido.
        rxCorrompidos++;
        Serial.printf("---,Fail,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;

    case RADIOLIB_ERR_RX_TIMEOUT:
        // A mensagem não foi respondida a tempo.
        rxPerdidos++;
        Serial.println("---,False,*;");
        break;

    default:
        // Um erro específico ocorreu ao receber a mensagem.
        rxPerdidos++;
        Serial.printf("---,False(%d),*;\n", recv.e);
        break;
    }

    radio.standby();
    return recv.e;
}

int16_t doReceiverLoop() {
    // E1 - Receber mensagem do receptor até ter um sucesso;
    recv_result_t recv = {0, 0, 0};

    Serial.println("---,+");

    do {
        // Tentar receber novamente em caso de erro da biblioteca
        if (recv.e != RADIOLIB_ERR_NONE) {
            Serial.println("[ERR] Tentando receber novamente em 2s...");
            delay(10);
        }

        recv = recvBlocking(3000);
    } while (recv.message == NULL && recv.e != RADIOLIB_ERR_NONE);

    // Imprimir o resultado da recepção no `Serial`
    switch (recv.e) {
    case RADIOLIB_ERR_NONE: {
        // Nenhum erro ocorreu ao receber o pacote.
        rxSucessos++;

        // E2 - Transmitir ACK para o transmissor até ter um sucesso.
        int16_t e = RADIOLIB_ERR_NONE;

        // Esperamos um tempo antes de transmitir a próxima mensagem para que o
        // receptor consiga mudar de estado a tempo.
        delay(RX_WAIT_DELAY);

        do {
            // Tentar receber novamente em caso de erro da biblioteca
            if (e != RADIOLIB_ERR_NONE) {
                Serial.println("[ERR] Tentando transmissao novamente em 2s...");
                delay(10);
            }

            e = sendBlocking((const uint8_t *)"0Resposta", 9);
        } while (e != RADIOLIB_ERR_NONE);

        Serial.printf("---,True,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;
    }

    case RADIOLIB_ERR_CRC_MISMATCH:
        rxCorrompidos++;
        Serial.printf("---,Fail,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;

    case RADIOLIB_ERR_RX_TIMEOUT:
        rxPerdidos++;
        Serial.println("---,False,*;");
        break;

    default:
        rxPerdidos++;
        Serial.printf("---,False(%d),*;\n", recv.e);
        break;
    }

    radio.standby();
    return recv.e;
}

// Desenha o cargo do ESP e o número atual do teste.
void drawTitle() {
    const char *roleStr = role == kTransmitter ? "Transmissor" : "Receptor";

    char cycleStr[64];

    display.setCursor(0, 0);
    display.fillRect(0, 0, display.width(), 10, WHITE);

    display.setTextColor(BLACK);
    drawAlignedText(&display, roleStr, 2, 2, kStart);

    // Desenhar contagem de ciclos
    snprintf(cycleStr, 64, "%d/%d", cyclesTotal + cycles,
             POSSIBLE_PARAMETERS * TESTS_PER_CYCLE);
    drawAlignedText(&display, cycleStr, 2, 2, kEnd);
}

// Desenha um relatório do estado atual dos testes na tela.
void drawReport() {
    char sfStr[8];
    char crStr[8];
    char rssiStr[16];
    char snrStr[16];

    char okStr[24];
    char crcStr[24];
    char lostStr[24];

    // Desenhar parâmetros
    display.setTextColor(WHITE);
    snprintf(sfStr, 8, "SF %hhu", dr.lora.spreadingFactor);
    snprintf(crStr, 8, "CR %hhu", dr.lora.codingRate);
    drawAlignedText(&display, sfStr, 2, 10, kStart, kEnd);
    drawAlignedText(&display, crStr, 2, 2, kStart, kEnd);

    // Desenhar RSSI e SNR
    snprintf(rssiStr, 16, "%.1f dBm", radio.getRSSI());
    snprintf(snrStr, 16, "%.1f dB", radio.getSNR());
    drawAlignedText(&display, "RSSI", 20, -12, kStart, kCenter);
    drawAlignedText(&display, "SNR", 20, -4, kStart, kCenter);
    drawAlignedText(&display, rssiStr, 20, -12, kEnd, kCenter);
    drawAlignedText(&display, snrStr, 20, -4, kEnd, kCenter);

    // Desenhar contagens atuais
    snprintf(okStr, 24, "%u ok  ", rxSucessos);
    snprintf(crcStr, 24, "%u crc ", rxCorrompidos);
    snprintf(lostStr, 24, "%u lost", rxPerdidos);
    drawAlignedText(&display, okStr, 2, 18, kEnd, kEnd);
    drawAlignedText(&display, crcStr, 2, 10, kEnd, kEnd);
    drawAlignedText(&display, lostStr, 2, 2, kEnd, kEnd);
}

void loop() {
    static enum { kSelection, kTesting } state = kSelection;

    button.loop(400, 600);

    const char *roleStr = role == kTransmitter ? "Transmissor" : "Receptor";

    switch (state) {
    case kSelection: {
        if (button.longPressed()) {
            role = role == kTransmitter ? kReceiver : kTransmitter;
        } else if (button.pressed()) {
            state = kTesting;
            Serial.printf("Cargo \"%s\" selecionado\n", roleStr);
            return;
        }

        display.clearDisplay();
        display.setTextColor(WHITE);

        const int16_t rectWidth = 11 * 8 + 2;
        drawAlignedText(&display, "Selecione o cargo", 0, -16, kCenter,
                        kCenter);

        display.fillRect((display.width() / 2) - (rectWidth / 2),
                         (display.height() / 2) - 4 +
                             (role == kTransmitter ? 10 : 0) - 1,
                         rectWidth, 10, WHITE);

        display.setTextColor(role == kReceiver ? BLACK : WHITE);
        drawAlignedText(&display, "Receptor", 0, 0, kCenter, kCenter);

        display.setTextColor(role == kTransmitter ? BLACK : WHITE);
        drawAlignedText(&display, "Transmissor", 0, 10, kCenter, kCenter);

        display.display();
        break;
    }

    case kTesting: {
        // Atualizar os parametros para a atual linha de testes
        updateParameters();

        // Desenhar tela de confirmação
        {
            char paramStr[32];

            display.clearDisplay();
            drawTitle();

            display.setTextColor(WHITE);
            drawAlignedText(&display, "Pressione", 0, -8, kCenter, kCenter);
            drawAlignedText(&display, "para iniciar", 0, 0, kCenter, kCenter);

            snprintf(paramStr, 16, "SF %d / CR %d", dr.lora.spreadingFactor,
                     dr.lora.codingRate);
            drawAlignedText(&display, paramStr, 0, 12, kCenter, kCenter);

            display.display();
        }

        while (!button.pressed()) {
            button.loop(400, 600);
        };

        while (cycles < TESTS_PER_CYCLE) {
            cycles++;

            // Desenhar tela de relatório
            display.clearDisplay();
            drawTitle();
            drawReport();
            display.display();

            // Executar um ciclo (transmitir -> receber resposta OU receber ->
            // transmitir resposta)
            int16_t result;
            if (role == kTransmitter)
                result = doTransmitterLoop();
            else
                result = doReceiverLoop();
        }

        // Iniciar próxima lista de testes ao chegar no ciclo 100.
        cyclesTotal += cycles;
        cycles = 0;

        Serial.println("Um ciclo foi completo!");

        break;
    }
    }
}
