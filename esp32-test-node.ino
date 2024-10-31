#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <cstdint>

// Des-comente a linha abaixo para compilar o servidor (envia o pacote de 250
// bytes). #define SERVER

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

#define LORA_NSS GPIO_NUM_8
#define LORA_DIO1 GPIO_NUM_14
#define LORA_RST GPIO_NUM_12
#define LORA_BUSY GPIO_NUM_13

SX1262 radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY);
Adafruit_SSD1306 display =
    Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RST);

bool didIrq = false;

// A ISR executada para qualquer evento LoRa, apenas seta a flag `didIrq`.
//
// O código que realmente lida com os interrupts é executado no `loop()`,
// pois, ao executá-lo direto na ISR, consistentemente causava crashes
// em testes.
void IRAM_ATTR setIrqFlag(void) {
    didIrq = true;
}

void setup() {
    Serial.begin(9600);

    // Inicializa o monitor OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3D, true, false);

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);

    // Inicializa o radio SX1262
    if (radio.begin(915.0, 62.5, 7, 5, 0x12, 20) != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Erro ao inicializar LoRa (cod. %d)\n", e);

        while (true) {
        };
    }

    // Define a ISR para quando uma mensagem for enviada/recebida.
    radio.setPacketReceivedAction(setIrqFlag);
    radio.setPacketSentAction(setIrqFlag);
}

enum state_t { kReceiving, kTransmitting } state;

// Conta quantos ciclos (transmitindo -> recebendo -> ...) ocorreram até agora.
uint32_t cycles = 0;
uint32_t cyclesTotal = 0;

uint32_t rxSucessos = 0;
uint32_t rxCorrompidos = 0;
uint32_t rxPerdidos = 0;

// O loop principal para o server.
void serverLoop() {
    int16_t e;

    if (state == kReceiving) {
        // Terminamos de receber a mensagem, ou ocorreu um erro na recepção.
        byte msg[RADIOLIB_SX126X_MAX_PACKET_LENGTH];

        int length = radio.getPacketLength();

        switch (e = radio.readData(msg, length)) {
        case RADIOLIB_ERR_NONE:
            // Nenhum erro ocorreu ao receber o pacote.
            rxSucessos++;
            Serial.println("---,+");
            break;

        case RADIOLIB_ERR_CRC_MISMATCH:
            rxCorrompidos++;
            Serial.printf(
                "[ERR] Um pacote corrompido foi recebido (%d bytes)\n", length);
            break;

        case RADIOLIB_ERR_RX_TIMEOUT:
            rxPerdidos++;
            Serial.println("[ERR] Um pacote não foi recebido.");
            break;

        default:
            rxPerdidos++;
            Serial.printf("[ERR] Erro ao receber mensagem (cod. %d)\n", e);
            break;
        }

        // Transicionar para o estado `kTransmitting`
        uint8_t response[] = "0Mensagem recebida.";
        e = radio.startTransmit(response, sizeof(response));

        if (e != RADIOLIB_ERR_NONE) {
            Serial.printf("[ERR] Erro ao transmitir resposta (cod. %d)\n", e);
        }

        state = kTransmitting;
    } else if (state == kTransmitting) {
        // Terminamos de transmitir a resposta.
        Serial.printf("---,%.0f,%.0f;\n", radio.getRSSI(), radio.getSNR());

        e = radio.startReceive(3000);
        if (e != RADIOLIB_ERR_NONE) {
            Serial.printf(
                "[ERR] Erro ao preparar para receber mensagem (cod. %d)\n", e);
        }

        state = kReceiving;
        cycles++;
    }
}

// O loop principal para o client.
void clientLoop() {
    int16_t e;

    if (state == kReceiving) {
        // Terminamos de receber a resposta, ou ocorreu um erro na recepção.
        byte msg[RADIOLIB_SX126X_MAX_PACKET_LENGTH];

        int length = radio.getPacketLength();

        switch (e = radio.readData(msg, length)) {
        case RADIOLIB_ERR_NONE:
            // Nenhum erro ocorreu ao receber o pacote.
            rxSucessos++;
            Serial.printf("---,True,%.0f,%.0f;\n", radio.getRSSI(),
                          radio.getSNR());
            break;

        case RADIOLIB_ERR_CRC_MISMATCH:
            // Um pacote corrompido foi recebido.
            rxCorrompidos++;
            Serial.printf("---,Fail,%.0f,%.0f;\n", radio.getRSSI(),
                          radio.getSNR());
            break;

        case RADIOLIB_ERR_RX_TIMEOUT:
            // A mensagem não foi respondida a tempo.
            rxPerdidos++;
            Serial.println("---,False,*;");
            break;

        default:
            // Um erro específico ocorreu ao receber a mensagem.
            rxPerdidos++;
            Serial.printf("---,False(%d),*;\n", e);
            break;
        }

        // Transicionar para o estado `kTransmitting`
        e = radio.startTransmit(mensagemLarga, sizeof(mensagemLarga));

        if (e != RADIOLIB_ERR_NONE) {
            Serial.printf("[ERR] Erro ao transmitir resposta (cod. %d)\n", e);
        }

        state = kTransmitting;
        cycles++;
    } else if (state == kTransmitting) {
        // Terminamos de transmitir a mensagem.
        Serial.println("---,+");

        e = radio.startReceive(3000);
        if (e != RADIOLIB_ERR_NONE) {
            Serial.printf(
                "[ERR] Erro ao preparar para receber mensagem (cod. %d)\n", e);
        }

        state = kReceiving;
    }
}

void loop() {
    if (!didIrq)
        return;

    didIrq = false;

#ifdef SERVER
    serverLoop();
#else
    clientLoop();
#endif

    // Iniciar próxima lista de testes ao chegar no ciclo 100.
    if (cycles < 100)
        return;

    totalCycles += cycles;
    cycles = 0;

    // TODO
}
