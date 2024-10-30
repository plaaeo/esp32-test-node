#include <Arduino.h>
#include <RadioLib.h>
#include <SPI.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <cstdint>

#define OLED_WIDTH 128
#define OLED_HEIGHT 64
#define OLED_SDA 17
#define OLED_SCL 18
#define OLED_RST 21

// SX1262 has the following connections
// 
// NSS pin:   GPIO_NUM_8
// DIO1 pin:  GPIO_NUM_14
// RST pin:   GPIO_NUM_12
// BUSY pin:  GPIO_NUM_13
SX1262 radio = new Module(GPIO_NUM_8, GPIO_NUM_14, GPIO_NUM_12, GPIO_NUM_13);
Adafruit_SSD1306 display = Adafruit_SSD1306(OLED_WIDTH, OLED_HEIGHT, &Wire, OLED_RST);

void halt() {
    while (true) { delay(10); }
}

enum alignment_t { kStart, kCenter, kEnd };

// Função útil para desenhar texto com alinhamento na tela
void drawAlignedText(const char *text, int16_t x, int16_t y,
                     alignment_t horizontal = alignment_t::kStart,
                     alignment_t vertical = alignment_t::kStart) {
    int16_t _x, _y;
    uint16_t w, h;

    display.getTextBounds(text, 0, 0, &_x, &_y, &w, &h);

    // Alinhar a posição do texto horizontalmente, de acordo com a regra
    // dada
    switch (horizontal) {
    case kCenter:
        x += (OLED_WIDTH - w) / 2;
        break;
    case kEnd:
        x = OLED_WIDTH - w - x;
        break;
    }

    // Alinhar a posição do texto verticalmente, de acordo com a regra dada
    switch (vertical) {
    case kCenter:
        y += (OLED_HEIGHT + h) / 2;
        break;
    case kEnd:
        y = OLED_HEIGHT - h - y;
        break;
    }

    display.setCursor(x, y);
    display.println(text);
}

bool recv = false;
void printPacket(void) {
    recv = true;
}

void setup() {
    // put your setup code here, to run once:
    Serial.begin(9600);

    Wire.begin(OLED_SDA, OLED_SCL);
    display.begin(SSD1306_SWITCHCAPVCC, 0x3C, true, false);    
    display.clearDisplay();

    display.setTextColor(WHITE);
    display.setTextSize(1);

    char buffer[256];

    int state = radio.begin(915.0, 125.0, 7, 5, 0x12, 15);
    if (state != RADIOLIB_ERR_NONE) {
        sprintf(buffer, "LoRa init failed");
        drawAlignedText(buffer, 0, -4, kCenter, kCenter);

        sprintf(buffer, "code %d");
        drawAlignedText(buffer, 0, 4, kCenter, kCenter);
        display.display();
        halt();
    }

    // set the function that will be called
    // when new packet is received
    radio.setPacketReceivedAction(printPacket);

    // start listening for LoRa packets
    state = radio.startReceive();
    if (state != RADIOLIB_ERR_NONE) {
        sprintf(buffer, "LoRa recv failed");
        drawAlignedText(buffer, 0, -4, kCenter, kCenter);

        sprintf(buffer, "code %d");
        drawAlignedText(buffer, 0, 4, kCenter, kCenter);
        display.display();
        halt();
    }
}

void loop() {
    char buffer[256];
    static uint32_t packetNumber = 0;

    // put your main code here, to run repeatedly:
    if (recv) {
        display.clearDisplay();

        byte packet[255];
        int numBytes = radio.getPacketLength();
        int state = radio.readData(packet, numBytes);

        snprintf(buffer, 255, "[ Packet %d ]", ++packetNumber);
        drawAlignedText(buffer, 0, -8, kCenter, kCenter);

        if (state == RADIOLIB_ERR_NONE) {
            // packet was successfully received
            snprintf(buffer, 255, "SNR: %.1f dB", radio.getSNR());
            drawAlignedText(buffer, 0, 12, kCenter, kCenter);

            display.fillRect(0, OLED_HEIGHT - 8, OLED_WIDTH, 8, WHITE);
            display.setTextColor(BLACK);
            snprintf(buffer, 255, "%16s", packet);
            drawAlignedText(buffer, 0, 0, kCenter, kEnd);
            
            display.setTextColor(WHITE);
            snprintf(buffer, 255, "RSSI: %.1f dBm", radio.getRSSI());
            
        } else if (state == RADIOLIB_ERR_CRC_MISMATCH) {
            snprintf(buffer, 255, "CRC mismatch");
        } else {
            snprintf(buffer, 255, "Error code %d", state);
        }

        drawAlignedText(buffer, 0, 4, kCenter, kCenter);
        display.display();
    }

    recv = false;
}
