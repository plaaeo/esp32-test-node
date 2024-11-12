#pragma once
#include "button.h"
#include "radio.h"
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>
#include <cstdint>

Adafruit_SSD1306 _display =
    Adafruit_SSD1306(DISPLAY_WIDTH, DISPLAY_HEIGHT, &Wire, RST_OLED);

button_t _button(GPIO_NUM_0, 400, 600);

/// @brief Quando `true`, pula a etapa de desenhar a UI caso nada tenha mudado.
bool _guiLazy = false;

enum screen_t {
    /// @brief Tela de seleção de cargo.
    kRoleSelection,
    /// @brief Tela de confirmação de início de teste.
    kConfirmTest,
    /// @brief Contagem regressiva para o início do teste.
    kCountdown,
    /// @brief Tela de reporte das estatísticas da última transmissão.
    kReport,
    /// @brief Tela de fim do teste.
    kEndScreen,
} _guiScreen;

screen_t guiProcess();

/// @return Uma string com o cargo do módulo (receptor/transmissor).
const char *guiRoleName() {
    return _moduleState.role == kTransmitter ? "Transmissor" : "Receptor";
}

/// @brief Inicializa o monitor OLED, o botão e o estado da interface gráfica.
void guiInit() {
    // Inicializa o monitor OLED
    Wire.begin(SDA_OLED, SCL_OLED);
    _display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);

    _display.clearDisplay();
    _display.setTextColor(WHITE);
    _display.setTextSize(1);

    // Inicializar o estado da interface
    _guiScreen = screen_t::kRoleSelection;
    _guiLazy = false;
}

/// @brief Atualiza a interface gráfica.
/// @returns `true` se o teste deve ser executado após esta função.
bool guiLoop() {
    _button.loop();

    screen_t next = guiProcess();

    // Garante que telas novas vão ser desenhadas pelo menos uma vez.
    _guiLazy = (next == _guiScreen);
    _guiScreen = next;

    return _guiScreen == screen_t::kReport;
}

enum alignment_t {
    kStart,
    kCenter,
    kEnd,
};

/// @brief Função útil para desenhar texto com alinhamento na tela
/// @param text O texto a ser desenhado.
/// @param x O offset x do texto.
/// @param y O offset y do texto.
/// @param horizontal O alinhamento horizontal do texto.
/// @param vertical O alinhamento vertical do texto.
void drawAlignedText(const char *text, int16_t x, int16_t y,
                     alignment_t horizontal = alignment_t::kStart,
                     alignment_t vertical = alignment_t::kStart) {
    int16_t _x, _y;
    uint16_t w, h;

    _display.getTextBounds(text, 0, 0, &_x, &_y, &w, &h);

    // Alinhar a posição do texto horizontalmente, de acordo com a regra
    // dada
    switch (horizontal) {
    case alignment_t::kCenter:
        x += (_display.width() - w) / 2;
        break;
    case alignment_t::kEnd:
        x = _display.width() - w - x;
        break;
    }

    // Alinhar a posição do texto verticalmente, de acordo com a regra dada
    switch (vertical) {
    case alignment_t::kCenter:
        y += (_display.height() - h) / 2;
        break;
    case alignment_t::kEnd:
        y = _display.height() - h - y;
        break;
    }

    _display.setCursor(x, y);
    _display.println(text);
}

/// @brief Desenha uma seta no canto superior esquerdo sinalizando se o módulo
/// está recebendo ou enviando.
void drawFeedback(bool isRecv) {
#ifndef DO_STATE_FEEDBACK
    return;
#endif

    _display.setTextColor(BLACK, WHITE);
    _display.setCursor(2, 1);

    if (isRecv) {
        _display.write('\x19');
    } else {
        _display.write('\x18');
    }

    _display.display();
}

/// @brief Desenha uma barra de título contendo o cargo do ESP, o número
/// do teste atual e o estado do módulo.
void drawTitle(const test_progress_t &test) {
    char progressStr[64];

    _display.setCursor(0, 0);
    _display.fillRect(0, 0, _display.width(), 10, WHITE);

    _display.setTextColor(BLACK);
    drawAlignedText(guiRoleName(), 10, 2, kStart);

    // Desenhar contagem de ciclos
    snprintf(progressStr, 64, "%d/%d", test.progress, TEST_COUNT);
    drawAlignedText(progressStr, 2, 2, kEnd);
}

/// @brief Desenha um relatório do estado atual dos testes na tela.
void drawReport(const test_progress_t &test) {
    char sfStr[8];
    char crStr[8];
    char rssiStr[16];
    char snrStr[16];

    char okStr[24];
    char crcStr[24];
    char lostStr[24];
    char toaStr[24];

    _display.setTextColor(WHITE);

    uint32_t transmitTime = radioTransmitTime();
    int16_t rssi = radioRSSI();
    float snr = radioSNR();

    // Encolher o ToA caso seja maior que um segundo.
    if (transmitTime >= 1000) {
        snprintf(toaStr, 24, "ToA* %.1fs", ((float)transmitTime) / 1000.0f);
    } else if (transmitTime == 0) {
        snprintf(toaStr, 24, "ToA* ...");
    } else {
        snprintf(toaStr, 24, "ToA* %dms", transmitTime);
    }

    // Desenhar SF, CR e ToA*
    snprintf(sfStr, 8, "SF %hhu", _moduleState.parameters.sf);
    snprintf(crStr, 8, "CR %hhu", _moduleState.parameters.cr);
    drawAlignedText(toaStr, 2, 18, kStart, kEnd);
    drawAlignedText(sfStr, 2, 10, kStart, kEnd);
    drawAlignedText(crStr, 2, 2, kStart, kEnd);

    // Desenhar RSSI e SNR
    snprintf(rssiStr, 16, "%hd dBm", rssi);
    snprintf(snrStr, 16, "%.1f dB", snr);
    drawAlignedText("RSSI", 20, -12, kStart, kCenter);
    drawAlignedText("SNR", 20, -4, kStart, kCenter);
    drawAlignedText(rssiStr, 20, -12, kEnd, kCenter);
    drawAlignedText(snrStr, 20, -4, kEnd, kCenter);

    // Desenhar contagens atuais
    snprintf(okStr, 24, "%u ok  ", test.successes);
    snprintf(crcStr, 24, "%u crc ", test.crcErrors);
    snprintf(lostStr, 24, "%u lost", test.losses);
    drawAlignedText(okStr, 2, 18, kEnd, kEnd);
    drawAlignedText(crcStr, 2, 10, kEnd, kEnd);
    drawAlignedText(lostStr, 2, 2, kEnd, kEnd);
}

/// @brief Desenha a interface gráfica.
/// @returns O próximo valor de `_guiScreen`
screen_t guiProcess() {
    switch (_guiScreen) {
    case kRoleSelection: {
        bool isTransmitter = (_moduleState.role == kTransmitter);

        if (_button.longPressed()) {
            // Trocar o estado selecionado ao segurar o botão
            _moduleState.role = isTransmitter ? kReceiver : kTransmitter;
            _guiLazy = false;
        } else if (_button.pressed()) {
            // Selecionar o estado ao pressionar o botão
            Serial.printf("Cargo \"%s\" selecionado\n", guiRoleName());
            return kConfirmTest;
        }

        if (_guiLazy)
            break;

        // Desenhar menu de seleção de cargo
        _display.clearDisplay();
        _display.setTextColor(WHITE);

        drawAlignedText("Selecione o cargo", 0, -16, kCenter, kCenter);

        // Desenha caixa de seleção do cargo
        const int16_t rectWidth = 11 * 8 + 2;
        _display.fillRect((_display.width() / 2) - (rectWidth / 2),
                          (_display.height() / 2) - 4 +
                              (isTransmitter ? 10 : 0) - 1,
                          rectWidth, 10, WHITE);

        _display.setTextColor(!isTransmitter ? BLACK : WHITE);
        drawAlignedText("Receptor", 0, 0, kCenter, kCenter);

        _display.setTextColor(isTransmitter ? BLACK : WHITE);
        drawAlignedText("Transmissor", 0, 10, kCenter, kCenter);

        _display.display();
        break;
    }

    case kConfirmTest: {
        char paramStr[32];

        // Iniciar contagem regressiva ao apertar o botão
        if (_button.pressed())
            return kCountdown;

        if (_guiLazy)
            break;

        _display.clearDisplay();
        drawTitle(_moduleState.wholeTest);

        _display.setTextColor(WHITE);
        drawAlignedText("Pressione", 0, -8, kCenter, kCenter);
        drawAlignedText("para iniciar", 0, 0, kCenter, kCenter);

        snprintf(paramStr, 32, "SF %d / CR %d", _moduleState.parameters.sf,
                 _moduleState.parameters.cr);

        drawAlignedText(paramStr, 0, 12, kCenter, kCenter);

        _display.display();
        break;
    }

    case kCountdown: {
        char tMinus[64];

        _display.clearDisplay();
        drawTitle(_moduleState.wholeTest);

        for (unsigned t = 3; t > 0; t--) {
            _display.setTextColor(WHITE, BLACK);

            snprintf(tMinus, 64, "Iniciando em %ds...", t);
            drawAlignedText(tMinus, 0, 0, kCenter, kCenter);

            _display.display();
            delay(1000);
        }

        return kReport;
    }

    case kReport: {
        // Desenhar tela de relatório
        _display.clearDisplay();
        drawTitle(_moduleState.wholeTest);
        drawReport(_moduleState.wholeTest);
        _display.display();

        break;
    }

    case kEndScreen: {
        test_progress_t &test = _moduleState.wholeTest;

        if (_guiLazy)
            break;

        // Desenhar tela de fim
        _display.clearDisplay();
        drawTitle(test);

        _display.setTextColor(WHITE);

        char progressStr[64];
        char okStr[24];
        char crcStr[24];
        char lostStr[24];

        snprintf(progressStr, 64, "%d testes", test.progress);
        snprintf(okStr, 24, "%d sucessos", test.successes);
        snprintf(crcStr, 24, "%d malformados", test.crcErrors);
        snprintf(lostStr, 24, "%d perdidos", test.losses);

        drawAlignedText("Fim!", 0, 4, kCenter, kCenter);
        drawAlignedText(progressStr, 0, 12, kCenter, kCenter);
        drawAlignedText(okStr, 24, 20, kStart, kCenter);
        drawAlignedText(crcStr, 24, 28, kStart, kCenter);
        drawAlignedText(lostStr, 24, 36, kStart, kCenter);

        _display.display();

        // Aguardar infinitamente
        while (true) {
        };
    }
    }

    return _guiScreen;
}

/// @brief Muda a tela atual da interface gráfica.
void guiGoto(screen_t screen) {
    _guiScreen = screen;
    _guiLazy = false;
};