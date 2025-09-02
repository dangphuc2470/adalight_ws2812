/*
 * Arduino interface for the use of WS2812 strip LEDs
 * Uses Adalight protocol and is compatible with Boblight, Prismatik etc...
 * "Magic Word" for synchronisation is 'Ada' followed by LED High, Low and Checksum
 * @author: Wifsimster <wifsimster@gmail.com> 
 * @library: FastLED v3.001
 * @date: 11/22/2015
 *
 * Modified by dangphuc2470 to include TFT display visualization.
 */
#include "FastLED.h"
#include <SPI.h>
#include <TFT_22_ILI9225.h>

// LED strip configuration
#define NUM_LEDS 240
#define DATA_PIN 6

// Baudrate, higher rate allows faster refresh rate and more LEDs (defined in /etc/boblight.conf)
#define serialRate 115200

// Adalight sends a "Magic Word" (defined in /etc/boblight.conf) before sending the pixel data
uint8_t prefix[] = {'A', 'd', 'a'}, hi, lo, chk, i;

// Initialise LED-array
CRGB leds[NUM_LEDS];

// TFT display configuration
// These are standard pinout definitions for a TFT, though you
// must connect them to the hardware SPI pins of your Arduino
#define TFT_CS    10    // Chip Select pin
#define TFT_RST   9     // Reset pin
#define TFT_RS    8     // Data/Command pin
#define TFT_SCLK  13    // SPI Clock (CLK) pin, hardware SPI pin on most Arduinos
#define TFT_MOSI  11    // SPI Data (MOSI) pin, hardware SPI pin on most Arduinos
#define LEFT_EDGE_LED 50    // Central LED index on the left edge of the strip
#define RIGHT_EDGE_LED 10   // Central LED index on the right edge of the strip
#define TOP_EDGE_LED 30     // Central LED index on the top edge of the strip
#define BOTTOM_EDGE_LED 90  // Central LED index on the bottom edge of the strip

TFT_22_ILI9225 tft = TFT_22_ILI9225(TFT_RST, TFT_RS, TFT_CS, -1);

static bool firstMessageReceived = false;

// Helper function to convert 8-bit RGB to 16-bit 565 format for the display
uint16_t color565(byte r, byte g, byte b) {
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

// Function to lighten a color for better contrast
CRGB getLightestColor(CRGB c) {
    // Find the maximum of the R, G, and B components
    uint8_t maxVal = max(c.r, max(c.g, c.b));

    // If all components are 0, return black
    if (maxVal == 0) {
        return CRGB(0, 0, 0);
    }

    // Calculate scaling factors and return the new color
    return CRGB(
            (uint8_t)((float)c.r / maxVal * 255),
            (uint8_t)((float)c.g / maxVal * 255),
            (uint8_t)((float)c.b / maxVal * 255)
    );
}

// Function to visualize colors from specific LEDs on the screen with text labels
void visualizeEdgeColors() {
    tft.setFont(Terminal11x16);

    const int screenWidth = 176;
    const int screenHeight = 220;

    // Apply the getLightestColor function to all edge colors for consistent readability
    CRGB lightestRight = getLightestColor(leds[LEFT_EDGE_LED]);
    CRGB lightestTop = getLightestColor(leds[RIGHT_EDGE_LED]);
    CRGB lightestLeft = getLightestColor(leds[TOP_EDGE_LED]);
    CRGB lightestBottom = getLightestColor(leds[BOTTOM_EDGE_LED]);

    // Right Edge
    uint16_t colorRight = color565(lightestRight.r, lightestRight.g, lightestRight.b);
    tft.drawText(screenWidth - 60, screenHeight / 2 - 8, "Right:", colorRight);

    // Top Edge
    uint16_t colorTop = color565(lightestTop.r, lightestTop.g, lightestTop.b);
    tft.drawText(screenWidth / 2 - 25, 120, "Top:", colorTop);

    // Left Edge
    uint16_t colorLeft = color565(lightestBottom.r, lightestBottom.g, lightestBottom.b);
    tft.drawText(10, screenHeight / 2 - 8, "Left:", colorLeft);

    // Bottom Edge
    uint16_t colorBottom = color565(lightestBottom.r, lightestBottom.g, lightestBottom.b);
    tft.drawText(screenWidth / 2 - 25, 60, "Bottom:", colorBottom);
}

void setup() {
    tft.begin();
    tft.setOrientation(1); // Set orientation
    tft.setBackgroundColor(COLOR_BLACK);
    tft.clear();

    // Show "Setting up..." message
    tft.setFont(Terminal11x16);
    tft.drawText(10, 100, "Setting Up...", COLOR_WHITE);

    // Initialise LED-array
    FastLED.addLeds<NEOPIXEL, DATA_PIN>(leds, NUM_LEDS);

    // Initialize the hardware SPI bus for the display
    SPI.begin();

    // Wait a moment before checking for a serial connection.
    // This helps differentiate a power-on from a serial port reset.
    delay(200);

    // Only run the test flash if there's no serial data coming in immediately.
    if (!Serial.available()) {
        // Initial RGB flash on the LED strip (for testing)
        FastLED.showColor(CRGB(255, 0, 0));
        delay(500);
        FastLED.showColor(CRGB(0, 255, 0));
        delay(500);
        FastLED.showColor(CRGB(0, 0, 255));
        delay(500);
        FastLED.showColor(CRGB(0, 0, 0));
    }

    Serial.begin(serialRate);
    // Send "Magic Word" string to host
    Serial.print("Ada\n");
}

void loop() {
    // Flush the serial buffer to discard old data and get ready for a new packet
    while(Serial.available()) {
        Serial.read();
    }

    // Wait for first byte of Magic Word
    for(i = 0; i < sizeof prefix; ++i) {
        waitLoop: while (!Serial.available()) ;;
        // Check next byte in Magic Word
        if(prefix[i] == Serial.read()) continue;
        // otherwise, start over
        i = 0;
        goto waitLoop;
    }

    // A successful "Magic Word" has been received. This means we are now
    // receiving data. We only need to clear the screen once.
    if (!firstMessageReceived) {
        tft.clear();
        firstMessageReceived = true;
    }

    // Hi, Lo, Checksum
    while (!Serial.available()) ;;
    hi=Serial.read();
    while (!Serial.available()) ;;
    lo=Serial.read();
    while (!Serial.available()) ;;
    chk=Serial.read();

    // If checksum does not match go back to wait
    if (chk != (hi ^ lo ^ 0x55)) {
        i=0;
        goto waitLoop;
    }

    memset(leds, 0, NUM_LEDS * sizeof(struct CRGB));

    // Read the transmission data and set LED values
    for (uint8_t i = 0; i < NUM_LEDS; i++) {
        byte r, g, b;
        while(!Serial.available());
        r = Serial.read();
        while(!Serial.available());
        g = Serial.read();
        while(!Serial.available());
        b = Serial.read();
        leds[i].r = r;
        leds[i].g = g;
        leds[i].b = b;
    }

    // Visualize the border colors on the screen
    visualizeEdgeColors();

    // Shows new values
    FastLED.show();
}
