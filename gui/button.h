#pragma once
#include <cstdint>

enum action_t {
    kIdle,
    kProcess,
    kWait,
};

enum button_state_t { kReleased, kPressed, kLongPressed, kHeld };

// Armazena o estado de um botão físico.
class button_t {
public:
    /// @brief Cria um novo botão.
    /// @param pin O pino do botão.
    /// @param longPressTime O tempo, em millisegundos, para contar um
    /// pressionamento longo.
    /// @param holdTime O tempo, em millisegundos, para contar uma segurada
    /// (deve ser maior que `longPressTime`)
    button_t(uint32_t pin, uint32_t longPressTime = 400,
             uint32_t holdTime = 600) {
        this->pin = pin;
        this->longPressTime = longPressTime;
        this->holdTime = holdTime;

        this->action = kIdle;
        this->lastEvent = 0;

        this->state = kReleased;
        this->buffer = kReleased;
    }

    /// @brief Inicializa o botão.
    void setup() {
        pinMode(this->pin, INPUT_PULLUP);
    }

    /// @brief Atualiza o estado interno do botão.
    void loop() {
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

                if (elapsed > this->longPressTime) {
                    this->state = kLongPressed;
                }

                if (elapsed > this->holdTime) {
                    this->state = kHeld;
                }
            }

            break;
        }
        }
    }

    /// @brief Detecta se o usuário está segurando o botão a mais de `holdTime`
    /// millisegundos. Caso esteja, o botão irá esperar mais `holdTime`
    /// millisegundos para detectar se o usuário ainda está segurando o botão.
    bool held() {
        if (this->state == kHeld) {
            this->state = kLongPressed;
            this->lastEvent = millis();
            return true;
        }

        return false;
    }

    /// @returns `true` no instante que é detectado que o usuário está
    /// segurando o botão a mais de `longPressTime` millisegundos, retornando
    /// `false` até que o usuário solte o botão.
    bool longPressed() {
        if (this->state == kLongPressed && this->action == kProcess) {
            this->action = kWait;
            return true;
        }

        return false;
    }

    /// @returns `true` no instante que o usuário solta o botão.
    bool pressed() {
        return consume(kPressed) == kPressed;
    }

public:
    uint32_t longPressTime;
    uint32_t holdTime;

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