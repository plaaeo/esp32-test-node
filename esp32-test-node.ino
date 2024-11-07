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

#define POSSIBLE_PARAMETERS (sfLen * crLen)

// Define o tempo que o receptor e o transmissor esperam antes de transmitir uma
// mensagem. Usado para permitir que o outro ESP comece a receber mensagens a
// tempo.
#define RX_WAIT_DELAY 100

// Define a quantidade de testes para cada configuração do SF e CR
#define TESTS_PER_CONFIG 100

// Comente para remover feedback sobre o estado do teste
#define DO_STATE_FEEDBACK

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

button_t button = button_t(BUTTON, 400, 600);

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

struct test_progress_t {
    // O número do teste atual (de 0 até TESTS_PER_CONFIG - 1)
    uint32_t progress;
    uint32_t successes;
    uint32_t crcErrors;
    uint32_t losses;
};

// Armazena as estatísticas para o teste atual (uma configuração)
test_progress_t currentTest;
// Armazena as estatísticas cumulativas para todos os testes (todas as
// configurações)
test_progress_t wholeTest{0, 0, 0, 0};

// Atualiza os parametros para o teste atual.
// Retorna o "índice" da configuração atual.
uint32_t updateParameters() {
    uint32_t index = wholeTest.progress / TESTS_PER_CONFIG;

    dr.lora.bandwidth = 250;
    dr.lora.codingRate = cr[index % crLen];
    dr.lora.spreadingFactor = sf[(index / crLen) % sfLen];

    radio.setDataRate(dr);

    return index;
}

uint8_t msgTransmitter[MAX_PACKET_LENGTH];
uint8_t msgReceiver[] = "0Mensagem recebida";

enum { kReceiver, kTransmitter } role = kReceiver;

void setup() {
    msgTransmitter[0] = '0';

    for (size_t i = 1; i < sizeof(msgTransmitter); i++) {
        msgTransmitter[i] = 'A';
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
    return (timeout * 1000000) / 15625;
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
    drawFeedback(false);

    do {
        if (e != RADIOLIB_ERR_NONE) {
            Serial.println("[ERR] Tentando transmissao novamente em 2s...");
            delay(10);
        }

        e = sendBlocking(msgTransmitter, sizeof(msgTransmitter));
    } while (e != RADIOLIB_ERR_NONE);

    Serial.print("---,");

    // E2 - Receber ACK do receptor até ter um sucesso
    //
    // Similarmente, um sucesso ocorre quando o módulo consegue entrar no estado
    // de "receptor" com sucesso.
    recv_result_t recv = {0, 0, 0};
    drawFeedback(true);

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
        currentTest.successes++;
        Serial.printf("---,True,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;

    case RADIOLIB_ERR_CRC_MISMATCH:
        // Um pacote corrompido foi recebido.
        currentTest.crcErrors++;
        Serial.printf("---,Fail,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;

    case RADIOLIB_ERR_RX_TIMEOUT:
        // A mensagem não foi respondida a tempo.
        currentTest.losses++;
        Serial.println("---,False,*;");
        break;

    default:
        // Um erro específico ocorreu ao receber a mensagem.
        currentTest.losses++;
        Serial.printf("---,False(%d),*;\n", recv.e);
        break;
    }

    radio.standby();
    return recv.e;
}

int16_t doReceiverLoop() {
    // E1 - Receber mensagem do receptor até ter um sucesso;
    Serial.println("---,+");

    recv_result_t recv = {0, 0, 0};
    drawFeedback(true);

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
        currentTest.successes++;

        // E2 - Transmitir ACK para o transmissor até ter um sucesso.
        int16_t e = RADIOLIB_ERR_NONE;

        Serial.print("---,");

        // Esperamos um tempo antes de transmitir a próxima mensagem para que o
        // receptor consiga mudar de estado a tempo.
        delay(RX_WAIT_DELAY);
        drawFeedback(false);

        do {
            // Tentar receber novamente em caso de erro da biblioteca
            if (e != RADIOLIB_ERR_NONE) {
                Serial.println("[ERR] Tentando transmissao novamente em 2s...");
                delay(10);
            }

            e = sendBlocking(msgReceiver, sizeof(msgReceiver));
        } while (e != RADIOLIB_ERR_NONE);

        Serial.printf("---,True,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;
    }

    case RADIOLIB_ERR_CRC_MISMATCH:
        currentTest.crcErrors++;
        Serial.printf("---,Fail,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());
        break;

    case RADIOLIB_ERR_RX_TIMEOUT:
        currentTest.losses++;
        Serial.println("---,False,*;");
        break;

    default:
        currentTest.losses++;
        Serial.printf("---,False(%d),*;\n", recv.e);
        break;
    }

    radio.standby();
    return recv.e;
}

// Desenha um feedback sobre o estado do teste atual na tela.
void drawFeedback(bool isRecv) {
#ifndef DO_STATE_FEEDBACK
    return;
#endif

    display.setTextColor(BLACK, WHITE);
    display.setCursor(2, 1);

    if (isRecv) {
        display.write('\x19');
    } else {
        display.write('\x18');
    }

    display.display();
}

// Desenha o cargo do ESP e o número atual do teste.
void drawTitle() {
    const char *roleStr = role == kTransmitter ? "Transmissor" : "Receptor";

    char progressStr[64];

    display.setCursor(0, 0);
    display.fillRect(0, 0, display.width(), 10, WHITE);

    display.setTextColor(BLACK);
    drawAlignedText(&display, roleStr, 10, 2, kStart);

    // Desenhar contagem de ciclos
    uint32_t progress = wholeTest.progress + currentTest.progress;
    snprintf(progressStr, 64, "%d/%d", progress,
             POSSIBLE_PARAMETERS * TESTS_PER_CONFIG);
    drawAlignedText(&display, progressStr, 2, 2, kEnd);
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
    char toaStr[24];

    display.setTextColor(WHITE);

    // Desenhar parâmetros
    size_t msgLength =
        role == kTransmitter ? sizeof(msgTransmitter) : sizeof(msgReceiver);
    RadioLibTime_t toaUs = radio.getTimeOnAir(msgLength);

    snprintf(toaStr, 24, "ToA %dms", toaUs / 1000);
    snprintf(sfStr, 8, "SF %hhu", dr.lora.spreadingFactor);
    snprintf(crStr, 8, "CR %hhu", dr.lora.codingRate);
    drawAlignedText(&display, toaStr, 2, 18, kStart, kEnd);
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
    uint32_t successes = wholeTest.successes + currentTest.successes;
    uint32_t crcErrors = wholeTest.crcErrors + currentTest.crcErrors;
    uint32_t losses = wholeTest.losses + currentTest.losses;
    snprintf(okStr, 24, "%u ok  ", successes);
    snprintf(crcStr, 24, "%u crc ", crcErrors);
    snprintf(lostStr, 24, "%u lost", losses);
    drawAlignedText(&display, okStr, 2, 18, kEnd, kEnd);
    drawAlignedText(&display, crcStr, 2, 10, kEnd, kEnd);
    drawAlignedText(&display, lostStr, 2, 2, kEnd, kEnd);
}

void loop() {
    static enum { kSelection, kTesting } state = kSelection;

    button.loop();

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

        // Desenhar menu de seleção de cargo
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

        if (wholeTest.progress >= (TESTS_PER_CONFIG * POSSIBLE_PARAMETERS)) {
            // Desenhar tela de fim
            display.clearDisplay();
            drawTitle();

            display.setTextColor(WHITE);

            char progressStr[64];
            char okStr[24];
            char crcStr[24];
            char lostStr[24];

            snprintf(progressStr, 64, "%d testes", wholeTest.progress);
            snprintf(okStr, 24, "%d sucessos", wholeTest.successes);
            snprintf(crcStr, 24, "%d malformados", wholeTest.crcErrors);
            snprintf(lostStr, 24, "%d perdidos", wholeTest.losses);

            drawAlignedText(&display, "Fim!", 0, 4, kCenter, kCenter);
            drawAlignedText(&display, progressStr, 0, 12, kCenter, kCenter);
            drawAlignedText(&display, okStr, 24, 20, kStart, kCenter);
            drawAlignedText(&display, crcStr, 24, 28, kStart, kCenter);
            drawAlignedText(&display, lostStr, 24, 36, kStart, kCenter);

            display.display();

            // Aguardar infinitamente
            while (true) {
            };
        } else {
            // Desenhar tela de confirmação
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
            button.loop();
        };

        // Esperar 3 segundos antes de iniciar os testes
        char tMinus[64];
        for (unsigned t = 3; t > 0; t--) {
            display.clearDisplay();
            drawTitle();

            display.setTextColor(WHITE);

            snprintf(tMinus, 64, "Iniciando em %ds...", t);
            drawAlignedText(&display, tMinus, 0, 0, kCenter, kCenter);

            display.display();
            delay(1000);
        }

        while (currentTest.progress < TESTS_PER_CONFIG) {
            currentTest.progress++;

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

        // Iniciar próxima lista de testes ao chegar no último teste.
        wholeTest.progress += currentTest.progress;
        wholeTest.successes += currentTest.successes;
        wholeTest.crcErrors += currentTest.crcErrors;
        wholeTest.losses += currentTest.losses;

        Serial.printf(
            "Um ciclo foi completo! (%d sucessos, %d corrompidos, %d perdas)\n",
            currentTest.successes, currentTest.crcErrors, currentTest.losses);

        currentTest = test_progress_t{0, 0, 0, 0};

        break;
    }
    }
}
