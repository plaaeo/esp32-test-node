#pragma once
#include "radio.h"

/// @brief Define o tempo que o receptor e o transmissor esperam antes de
/// transmitir uma mensagem. Usado para permitir que o outro ESP comece a
/// receber mensagens a tempo.
#define RX_WAIT_DELAY 100

/// @brief Define a quantidade de testes para cada combinação de parâmetros.
#define TESTS_PER_CONFIG 100

const uint32_t sf[] = {7, 8, 9, 10, 11, 12};
const uint32_t cr[] = {5, 8};

#define POSSIBLE_SF (sizeof(sf) / sizeof(uint32_t))
#define POSSIBLE_CR (sizeof(cr) / sizeof(uint32_t))

#define POSSIBLE_PARAMETERS (POSSIBLE_SF * POSSIBLE_CR)
#define TEST_COUNT (POSSIBLE_PARAMETERS * TESTS_PER_CONFIG)

struct test_progress_t {
    // O número do teste atual (de 0 até TESTS_PER_CONFIG - 1)
    uint32_t progress;
    uint32_t successes;
    uint32_t crcErrors;
    uint32_t losses;
};

enum role_t {
    kReceiver,
    kTransmitter,
};

struct {
    role_t role;
    test_progress_t currentTest;
    test_progress_t wholeTest;
    radio_parameters_t parameters;
} _moduleState;