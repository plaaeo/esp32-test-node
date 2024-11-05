#pragma once
#include <Adafruit_GFX.h>
#include <cstdint>

enum alignment_t { kStart, kCenter, kEnd };

// Função útil para desenhar texto com alinhamento na tela
void drawAlignedText(Adafruit_GFX *display, const char *text, int16_t x,
                     int16_t y, alignment_t horizontal = alignment_t::kStart,
                     alignment_t vertical = alignment_t::kStart) {
    int16_t _x, _y;
    uint16_t w, h;

    display->getTextBounds(text, 0, 0, &_x, &_y, &w, &h);

    // Alinhar a posição do texto horizontalmente, de acordo com a regra
    // dada
    switch (horizontal) {
    case alignment_t::kCenter:
        x += (display->width() - w) / 2;
        break;
    case alignment_t::kEnd:
        x = display->width() - w - x;
        break;
    }

    // Alinhar a posição do texto verticalmente, de acordo com a regra dada
    switch (vertical) {
    case alignment_t::kCenter:
        y += (display->height() - h) / 2;
        break;
    case alignment_t::kEnd:
        y = display->height() - h - y;
        break;
    }

    display->setCursor(x, y);
    display->println(text);
}

#define LONG_PRESS_DURATION 200

enum action_t {
    kIdle,
    kProcess,
    kWait,
};

enum button_state_t { kReleased, kPressed, kLongPressed, kHeld };

// Armazena o estado de um botão físico.
class button_t {
public:
    button_t(uint32_t pin) {
        this->pin = pin;

        this->action = kIdle;
        this->lastEvent = 0;

        this->state = kReleased;
        this->buffer = kReleased;
    }

    // Inicializa o botão
    void setup() {
        pinMode(this->pin, INPUT_PULLUP);
    }

    // Atualiza o estado da estrutura para refletir o estado do botão.
    void loop(uint32_t longPressTime, uint32_t holdTime) {
        bool pressed = digitalRead(this->pin) == LOW;

        switch (this->action) {
        // Aguarda até o botão ser solto
        case kWait: {
            if (!pressed) {
                this->action = kIdle;
                this->state = kReleased;
            }

            break;
        }

        // Inicia o processamento da entrada caso o botão esteja solto
        case kIdle: {
            if (pressed) {
                this->action = kProcess;
                this->state = kPressed;
                this->lastEvent = millis();
            }

            break;
        }

        // Processa a entrada atual e retorna ao estado `kIdle` ao soltar o
        // botão
        case kProcess: {
            if (!pressed) {
                this->buffer = this->state;
                this->state = kReleased;
                this->action = kIdle;
            } else {
                uint32_t now = millis();
                uint32_t elapsed = now - this->lastEvent;

                if (elapsed > longPressTime) {
                    this->state = kLongPressed;
                }

                if (elapsed > holdTime) {
                    this->state = kHeld;
                }
            }

            break;
        }
        }
    }

    // Detecta se o usuário está segurando o botão a mais de `holdTime`
    // millisegundos. Caso true seja passado como parâmetro para o reset, o
    // botão irá reiniciar o contador e esperar novamente para detectar se o
    // usuário ainda está segurando o botão.
    bool held() {
        if (this->state == kHeld) {
            this->state = kLongPressed;
            this->lastEvent = millis();
            return true;
        }

        return false;
    }

    // Retorna true no instante que é detectado que o usuário está segurando o
    // botão a mais de `longPressTime` millisegundos, retornando `false` até que
    // o usuário solte o botão.
    bool longPressed() {
        if (this->state == kLongPressed && this->action == kProcess) {
            this->action = kWait;
            return true;
        }

        return false;
    }

    // Retorna true no instante que o usuário solta o botão
    bool pressed() {
        return consume(kPressed) == kPressed;
    }

private:
    button_state_t consume(button_state_t condition) {
        button_state_t buffer = this->buffer;

        if (buffer == condition) {
            this->buffer = kReleased;
        }

        return buffer;
    }

    uint32_t pin;

    action_t action;
    uint32_t lastEvent;

    button_state_t state;
    button_state_t buffer;
};