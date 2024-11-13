#pragma once
#include "SX126x.h"
#include <SPI.h>
#include <cstdint>

/**
 * Este arquivo serve apenas para abstrair as funções que interagem com o radio
 * LoRa para que seja fácil substituir a biblioteca a ser utilizada.
 *
 * Atualmente, a biblioteca utilizada é uma fork da LoRa-RF
 * (https://github.com/chandrawi/LoRaRF-Arduino)
 * A biblioteca aparenta ter sido abandonada, e um bug não permite a transmissão
 * direta de pacotes grandes. O fork apenas corrige este bug.
 */

SX126x _radio;

enum radio_error_t {
    kNone,

    /// @brief A mensagem recebida chegou corrompida.
    kCrc,

    /// @brief A mensagem recebida veio com o header inválido.
    kHeader,

    /// @brief Houve um timeout na operação desejada.
    kTimeout,

    /// @brief Houve um erro de tipo inesperado.
    kUnknown,
};

radio_error_t convertError(uint8_t e) {
    switch (e) {
    case LORA_STATUS_TX_TIMEOUT:
    case LORA_STATUS_RX_TIMEOUT:
        return kTimeout;

    case LORA_STATUS_HEADER_ERR:
        return kHeader;

    case LORA_STATUS_CRC_ERR:
        return kCrc;

    case LORA_STATUS_DEFAULT:
    case LORA_STATUS_TX_DONE:
    case LORA_STATUS_RX_DONE:
    case LORA_STATUS_CAD_DONE:
        return kNone;

    default:
        return kUnknown;
    }
}

/// @brief Contém a maioria dos parametros do radio LoRa, exceto pela
/// frequência e pelo LDRO (Low Data Rate Optimization), que é calculado
/// automaticamente.
struct radio_parameters_t {
    /// @brief Determina a sensibilidade extra do receptor, `false` para o modo
    /// de economia de energia.
    bool boostedRxGain;

    /// @brief Comprimento padrão do pacote. Caso maior que 0, o modo de header
    /// implícito é ligado.
    uint32_t packetLength;

    uint16_t preambleLength;
    uint32_t bandwidth;
    uint8_t sf;
    uint8_t cr;
    bool crc;
    bool invertIq;
};

/// @brief Inicializa o radio LoRa.
/// @returns `true` caso o radio tenha inicializado com sucesso.
bool radioInit() {
    SPI.begin(SCK, MISO, MOSI, SS);

    if (!_radio.begin(SS, RST_LoRa, BUSY_LoRa, DIO0, -1, -1))
        return false;

	  _radio.setDio3TcxoCtrl(SX126X_DIO3_OUTPUT_1_8, SX126X_TCXO_DELAY_10);
    _radio.setFrequency(915000000);
    _radio.setTxPower(22, SX126X_TX_POWER_SX1262);
    _radio.setSyncWord(0x3444);

    return true;
}

/// @brief Envia um pacote e aguarda ele terminar de ser enviado.
/// @param message A mensagem a ser enviada.
/// @param size O tamanho da mensagem
/// @return O resultado da operação.
radio_error_t radioSend(uint8_t *message, uint8_t size, uint32_t timeout = 0) {
    _radio.beginPacket();
    _radio.write(message, size);

    // Falha apenas se for chamado enquanto outra mensagem ainda estiver sendo
    // enviada, logo, não deve acontecer.
    if (!_radio.endPacket(timeout))
        return kUnknown;

    // Aguarda o pacote ser enviado completamente, ou demore demais e cause um
    // timeout.
    if (!_radio.wait())
        return kTimeout;

    uint8_t status = _radio.status();
    return convertError(status);
}

/// @brief Aguarda até que um pacote seja recebido, ou ocorra timeout.
/// @param dest O buffer onde a mensagem será armazenada.
/// @param length Deve ser passado com um valor inicial igual à capacidade
/// máxima do buffer `dest`, e será atualizado para conter o comprimento da
/// mensagem.
/// @param timeout O tempo máximo para receber o pacote, em millisegundos
/// @return O resultado da operação.
radio_error_t radioRecv(uint8_t *dest, uint8_t *length, uint32_t timeout = 0) {
    _radio.request(timeout);
    _radio.wait();

    uint8_t recvLength = _radio.available();

    if (recvLength < *length)
        *length = recvLength;
    
    // Ler o máximo possível que caiba em `dest`
    _radio.read(dest, *length);

    // Limpar o resto da mensagem que não foi lida
    _radio.purge(recvLength - *length);

    uint8_t status = _radio.status();
    return convertError(status);
}

/// @return O tempo, em millisegundos, que durou a transmissão da última
/// mensagem. Diferente do "Time on Air", que é calculado com base nos
/// parâmetros, o tempo de transmissão é calculado diretamente medindo o tempo
/// que demora até o sinal de `TX_DONE` ser recebido do módulo.
uint32_t radioTransmitTime() {
    return _radio.transmitTime();
}

/// @return O RSSI da última mensagem recebida.
int16_t radioRSSI() {
    return _radio.packetRssi();
}

/// @return O SNR da última mensagem recebida.
float radioSNR() {
    return _radio.snr();
}

/// @brief Atualiza os parâmetros do radio LoRa.
/// @param param Os parâmetros novos do radio.
void radioSetParameters(radio_parameters_t &param) {
    _radio.setLoRaPacket(param.packetLength > 0 ? SX126X_HEADER_IMPLICIT
                                                : SX126X_HEADER_EXPLICIT,
                         param.preambleLength, param.packetLength, param.crc,
                         param.invertIq);

    // https://github.com/sandeepmistry/arduino-LoRa/issues/85#issuecomment-372644755
    long ldro = 1000 / (param.bandwidth / (1u << param.sf));

    _radio.setLoRaModulation(param.sf, param.bandwidth, param.cr, ldro > 16);

    _radio.setRxGain(param.boostedRxGain ? SX126X_RX_GAIN_BOOSTED
                                         : SX126X_RX_GAIN_POWER_SAVING);
}