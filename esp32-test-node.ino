#include <Arduino.h>
#include <cstdint>

#include "gui/common.h"
#include "gui/gui.h"
#include "gui/radio.h"
#include "pins_arduino.h"

/// @brief Define o tempo que o receptor e o transmissor esperam antes de
/// transmitir uma mensagem. Usado para permitir que o outro ESP comece a
/// receber mensagens a tempo.
#define RX_WAIT_DELAY 60

/// @brief Atualiza os parametros para o teste atual.
/// @returns O "índice" da configuração atual.
uint32_t updateParameters() {
    uint32_t index = _moduleState.wholeTest.progress / TESTS_PER_CONFIG;

    _moduleState.parameters.bandwidth = 250000;
    _moduleState.parameters.cr = cr[index % POSSIBLE_CR];
    _moduleState.parameters.sf = sf[(index / POSSIBLE_CR) % POSSIBLE_SF];

    radioSetParameters(_moduleState.parameters);

    return index;
}

uint8_t msgTransmitter[255];
uint8_t msgReceiver[] = "0Mensagem recebida";

void setup() {
    // Inicializar os parâmetros padrão da transmissão LoRa
    _moduleState.parameters.boostedRxGain = false;
    _moduleState.parameters.packetLength = 0;
    _moduleState.parameters.preambleLength = 8;
    _moduleState.parameters.bandwidth = 250000;
    _moduleState.parameters.sf = 7;
    _moduleState.parameters.cr = 5;
    _moduleState.parameters.crc = true;
    _moduleState.parameters.invertIq = false;

    // Modifique o `progress` abaixo para iniciar o teste completo a partir de um índice especifico.
    _moduleState.wholeTest = test_progress_t {
        .progress = 0,
        .successes = 0,
        .crcErrors = 0,
        .losses = 0,
    };

    _moduleState.currentTest = test_progress_t {
        .progress = _moduleState.wholeTest.progress % TESTS_PER_CONFIG,
        .successes = 0,
        .crcErrors = 0,
        .losses = 0,
    };

    Serial.begin(115200);

    // Inicializar a mensagem do transmissor com "0AAAAA....."
    msgTransmitter[0] = '0';

    for (size_t i = 1; i < sizeof(msgTransmitter); i++) {
        msgTransmitter[i] = 'A';
    }

    // Inicializa o módulo SX126X LoRa
    if (!radioInit()) {
        Serial.println("Falha ao inicializar modulo LoRa.");
        while (true) {
        };
    };

    // Inicializa a interface gráfica
    guiInit();

    // Prepara os parâmetros para o primeiro teste
    updateParameters();

    Serial.println("Inicializado com sucesso.");
}

radio_error_t doTransmitterLoop() {
    uint8_t buffer[255];
    uint8_t length = 255;

    // Esperamos um tempo antes de transmitir a próxima mensagem para que o
    // receptor consiga mudar de estado a tempo.
    delay(RX_WAIT_DELAY);
    drawFeedback(false);

    // Transmitir uma mensagem grande para o receptor.
    Serial.println("---,+");
    Serial.flush();

    radioSend(msgTransmitter, sizeof(msgTransmitter));

    Serial.print("---,");
    Serial.flush();

    // Esperar até receber ACK do receptor, ou ocorrer um timeout em 3000ms
    drawFeedback(true);

    radio_error_t recvError = radioRecv(buffer, &length, 3000);
    recvError = (length != sizeof(msgReceiver) ? kCrc : recvError);
    switch (recvError) {
    case kNone:
        // Nenhum erro ocorreu ao receber o pacote.
        Serial.printf("---,True,%hd,%.0f;\n", radioRSSI(), radioSNR());
        break;

    case kCrc:
    case kHeader:
        // Um pacote corrompido foi recebido.
        Serial.printf("---,Fail,%hd,%.0f;\n", radioRSSI(), radioSNR());
        break;

    case kTimeout:
        // A mensagem não foi respondida a tempo.
        Serial.println("---,False,*;");
        break;

    default:
        // Um erro específico ocorreu ao receber a mensagem.
        Serial.println("---,False(!),*;");
        break;
    }

    Serial.flush();
    return recvError;
}

radio_error_t doReceiverLoop() {
    uint8_t buffer[255];
    uint8_t length = 255;

    // Esperar até receber mensagem do transmissor, ou ocorrer um timeout em
    // 3000ms
    drawFeedback(true);
    Serial.println("---,+");
    Serial.flush();

    radio_error_t recvError = radioRecv(buffer, &length, 3000);

    Serial.print("---,");
    Serial.flush();
    recvError = (length != sizeof(msgTransmitter) ? kCrc : recvError);
    // Imprimir o resultado da recepção no `Serial`
    switch (recvError) {
    case kNone:
        // Esperamos um tempo antes de transmitir a próxima mensagem para que o
        // receptor consiga mudar de estado a tempo.
        delay(RX_WAIT_DELAY);
        drawFeedback(false);

        // Enviar o ACK para o transmissor.
        radioSend(msgReceiver, sizeof(msgReceiver));
        
        Serial.printf("---,True,%hd,%.0f;\n", radioRSSI(), radioSNR());
        break;
    case kCrc:
    case kHeader:
        Serial.printf("---,Fail,%hd,%.0f;\n", radioRSSI(), radioSNR());
        break;
    case kTimeout:
        Serial.println("---,False,*;");
        break;
    default:
        Serial.println("---,False(!),*;");
        break;
    }

    Serial.flush();

    return recvError;
}

void loop() {
    // `guiLoop` retorna true quando estivermos na tela de reprodução de testes.
    if (!guiLoop())
        return;

    // Executar um ciclo (transmitir -> receber resposta OU receber ->
    // transmitir resposta)
    radio_error_t result;
    if (_moduleState.role == kTransmitter) {
        result = doTransmitterLoop();
    } else {
        result = doReceiverLoop();
    }

    // Armazenar os resultados do teste
    switch (result) {
    case kNone:
        _moduleState.currentTest.successes++;
        _moduleState.wholeTest.successes++;
        break;
    case kCrc:
    case kHeader:
        _moduleState.currentTest.crcErrors++;
        _moduleState.wholeTest.crcErrors++;
        break;
    default:
        _moduleState.currentTest.losses++;
        _moduleState.wholeTest.losses++;
        break;
    }

    _moduleState.currentTest.progress++;
    _moduleState.wholeTest.progress++;

    if (_moduleState.wholeTest.progress == TEST_COUNT) {
        // Entrar no estado final da interface ao terminar todos os testes.
        guiGoto(screen_t::kEndScreen);
    } else if (_moduleState.currentTest.progress / TESTS_PER_CONFIG) {
        // Uma lista completa de testes foi feita!
        Serial.printf("Um ciclo foi completo! [SF%d/CR%d] (%d sucessos, %d "
                      "corrompidos, %d perdas)\n",
                      _moduleState.parameters.sf, _moduleState.parameters.cr,
                      _moduleState.currentTest.successes,
                      _moduleState.currentTest.crcErrors,
                      _moduleState.currentTest.losses);

        _moduleState.currentTest = test_progress_t{};

        // Atualiza os parâmetros para o próximo teste
        updateParameters();
        guiGoto(screen_t::kConfirmTest);
    }
}
