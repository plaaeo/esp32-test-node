#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <cstdint>
#include "pins_arduino.h"

#define RX_WAIT_DELAY 100

// Definir pinos dependendo da placa
#if defined(WIFI_LoRa_32_V2)
    #define Radio     SX1276
    #define MAX_PACKET_LENGTH RADIOLIB_SX127X_MAX_PACKET_LENGTH

    #define LORA_NSS  SS
    #define LORA_DIO1 DIO0 // 4
    #define LORA_RST  RST_LoRa // 12
    #define LORA_BUSY DIO1 // 16

    #define OLED_SDA  GPIO_NUM_4
    #define OLED_SCL  GPIO_NUM_15
    #define OLED_RST  GPIO_NUM_16
    #define BUTTON    GPIO_NUM_0
#elif defined(WIFI_LoRa_32_V3)
    #define SERVER
    #define Radio     SX1262
    #define MAX_PACKET_LENGTH RADIOLIB_SX126X_MAX_PACKET_LENGTH

    #define LORA_NSS  8  // SS // 8
    #define LORA_DIO1 14 // DIO0 // 14
    #define LORA_RST  12 // RST_LoRa // 12
    #define LORA_BUSY 13 // BUSY_LoRa // 13

    #define OLED_SDA  GPIO_NUM_17
    #define OLED_SCL  GPIO_NUM_18
    #define OLED_RST  GPIO_NUM_21
    #define BUTTON    GPIO_NUM_0
#endif

#define OLED_WIDTH 128
#define OLED_HEIGHT 64

SPIClass spiSX(SPI);
SPISettings spiSettings(2000000, MSBFIRST, SPI_MODE0);
Radio radio = new Module(LORA_NSS, LORA_DIO1, LORA_RST, LORA_BUSY, spiSX, spiSettings);

Adafruit_SSD1306 display = Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RST);

uint8_t largeMessage[RADIOLIB_SX126X_MAX_PACKET_LENGTH];
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
    while (!didIrq) { };
    
    delay(10);
    didIrq = false;
}

void setup() {
    largeMessage[0] = '0';

    for (size_t i = 1; i < sizeof(largeMessage); i++) {
        largeMessage[i] = 'A';
    }

    Serial.begin(115200);
    spiSX.begin(SCK, MISO, MOSI, SS);

    // Inicia o botão programável
    pinMode(BUTTON, INPUT_PULLUP);

    // Inicializa o monitor OLED
    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);

    display.clearDisplay();
    display.setTextColor(WHITE);
    display.setTextSize(1);
    
    // Inicializa o radio LoRa
    int16_t e = radio.begin(915.0, 62.5, 7, 5, 0x12, 10);
    if (e != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Erro ao inicializar LoRa (cod. %d)\n", e);

        while (true) { };
    }

    // Define a ISR para quando uma mensagem for enviada/recebida.
    radio.setPacketReceivedAction(setIrqFlag);
    radio.setPacketSentAction(setIrqFlag);

    Serial.println("Inicializado com sucesso.");
}

// Conta quantos ciclos (transmitindo -> recebendo -> ...) ocorreram até agora.
uint32_t cycles = 0;
uint32_t cyclesTotal = 0;

uint32_t rxSucessos = 0;
uint32_t rxCorrompidos = 0;
uint32_t rxPerdidos = 0;

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
// Retorna e imprime no `Serial` qualquer erro que ocorrer durante a transmissão.
int16_t sendBlocking(const uint8_t* message, uint8_t size) {
    int16_t e = radio.startTransmit(message, size);

    if (e != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Erro ao transmitir mensagem (cod. %d)\n", e);
        // radio.finishTransmit();
        // radio.standby();
        return e;
    }

    // Esperar o pacote terminar de ser enviado.
    waitForIrq();

    // e = radio.finishTransmit();
    // if (e != RADIOLIB_ERR_NONE) {
    //     Serial.printf("[ERR] Erro ao finalizar transmissao (cod. %d)\n", e);
    // }

    return e;// radio.standby();
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
        Serial.printf("[ERR] Erro ao preparar para receber mensagem (cod. %d)\n", res.e);
        // radio.standby();
        
        return res;
    }

    // Esperar o pacote terminar de ser enviado.
    waitForIrq();

    res.message = message;
    res.length = radio.getPacketLength();
    res.e = radio.readData(message, res.length);

    if (res.e != RADIOLIB_ERR_NONE) {
        Serial.printf("[ERR] Erro ao receber mensagem (cod. %d)\n", res.e);
        // radio.standby();
        
        return res;
    }
    
    // res.e = radio.standby();
    return res;
}

void doTransmitterLoop() {
    // E1 - Tentar enviar o pacote grande repetidamente até ter um sucesso;
    // 
    // ATENÇÃO: Um sucesso significa que o módulo conseguiu transmitir a mensagem completa,
    // não significa que o receptor conseguiu receber a mensagem.
    int16_t e = RADIOLIB_ERR_NONE;
    
    Serial.println("---,+");

    // Esperamos um tempo antes de transmitir a próxima mensagem para que o receptor consiga
    // mudar de estado a tempo.
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
    // Similarmente, um sucesso ocorre quando o módulo consegue entrar no estado de "receptor"
    // com sucesso.
    recv_result_t recv = { 0,0,0 };

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
}

void doReceiverLoop() {
    // E1 - Receber mensagem do receptor até ter um sucesso;
    recv_result_t recv = { 0,0,0 };

    Serial.println("---,+");

    do {
        if (recv.e != RADIOLIB_ERR_NONE) {
            // Serial.println("[ERR] Tentando receber novamente em 2s...");
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

        // Esperamos um tempo antes de transmitir a próxima mensagem para que o receptor consiga
        // mudar de estado a tempo.
        delay(RX_WAIT_DELAY);

        do {
            if (e != RADIOLIB_ERR_NONE) {
                Serial.println("[ERR] Tentando transmissao novamente em 2s...");
                delay(10);
            }
            
            e = sendBlocking((const uint8_t*)"0Resposta", 9);
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
}

DataRate_t dr;

// Atualiza os parametros para o teste atual.
void updateParameters() {
    const uint32_t sf[] = { 7, 8, 9, 10, 11, 12 };
    const uint32_t cr[] = { 5, 8 };
    
    const size_t sfLen = sizeof(sf) / sizeof(uint32_t);
    const size_t crLen = sizeof(cr) / sizeof(uint32_t);

    uint32_t index = cyclesTotal / 100;

    dr.lora.bandwidth = 62.5;
    dr.lora.codingRate = cr[index % crLen];
    dr.lora.spreadingFactor = sf[(index / crLen) % sfLen];

    radio.setDataRate(dr);
}

void loop() {
    // Atualizar os parametros para a atual linha de testes
    updateParameters();
    
    display.clearDisplay();
    display.setCursor(0, 0);

    #ifdef SERVER
        display.println("[Transmissor]");
    #else
        display.println("[Receptor]");
    #endif

    display.printf("Ciclo %d", cyclesTotal);
    display.setCursor(0, 10);
    display.println("Aguardando...");
    display.printf("SF%d / CR%d", dr.lora.spreadingFactor, dr.lora.codingRate);

    display.display();

    while (digitalRead(BUTTON) != LOW);

    do {
        display.clearDisplay();
        display.setCursor(0, 0);
        
        #ifdef SERVER
            display.println("[Transmissor]");
        #else
            display.println("[Receptor]");
        #endif
        
        display.printf("Ciclo %d", cyclesTotal);
        
        display.setCursor(0, 10);
        display.printf("SF%d / CR%d", dr.lora.spreadingFactor, dr.lora.codingRate);

        #ifdef SERVER
        doTransmitterLoop();
        #else
        doReceiverLoop();
        #endif

        display.display();
    } while (cycles++ < 100);

    // Iniciar próxima lista de testes ao chegar no ciclo 100.
    cyclesTotal += cycles;
    cycles = 0;

    Serial.println("Um ciclo foi completo!");
}
