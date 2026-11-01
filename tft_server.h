// Example serial screenshot server implementation for Arduino using
// TFT_eSPI library.
//
// To use:
// * Copy tft_server.h and tft_server.cxx into your sketch folder.
// * Include the following edits to your main sketch:
//
// #define SCREEN_CAPTURE
// #ifdef SCREEN_CAPTURE
//     #include "tft_server.h"
// #endif
// ...
// TFT_eSPI g_tft;
// #ifdef SCREEN_CAPTURE
// uint8_t g_captureBuf[DISPLAY_WIDTH * DISPLAY_HEIGHT * 3];
// TFTServer g_tftServer(&g_tft, g_captureBuf, sizeof(g_captureBuf));
// #endif
// ...
// void setup(void)
// {
//     // Set serial speed to match the value used in your serial_screenshot_client.
//     // You can make this conditional on SCREEN_CAPTURE if you dont want to
//     // always run this fast.
//     Serial.begin(921600);
// ...
//
//     void loop() {
// #ifdef SCREEN_CAPTURE
//         g_tftServer.run();
// #endif
// ...
//
// Then comment or uncomment the SCREEN_CAPTURE define to control whether the
// server and the screenshot buffer are enabled.

#pragma once
#include <stdint.h>

class TFT_eSPI;

class TFTServer
{
public:
    // buf should be big enough for entire screenshot bitmap.
    TFTServer(TFT_eSPI* tft, uint8_t* buf, size_t bufLen);
    ~TFTServer();

    bool run();

private:
    // Rate control. Transfer IMAGE_XFER_BYTES every IMAGE_XFER_PERIOD_MS.
    // Modify these params (as well as Serial baudrate) to control transfer
    // rate, efficiency and CPU utilization.
    static constexpr uint32_t IMAGE_XFER_PERIOD_MS = 1;
    static constexpr uint32_t IMAGE_XFER_BYTES = 32;

    void setTimeout(uint32_t timeout);
    bool checkTimeout() const;
    bool screenshot();
    void sendHeader();

    enum State
    {
        STATE_INIT,
        STATE_FLUSH,
        STATE_READY,
        STATE_HDR,
        STATE_IMG,
    };
    State m_state = STATE_INIT;

    // Timer state
    uint32_t m_start = 0;
    uint32_t m_timeout = 0;

    // Pointer to display driver
    TFT_eSPI* m_tft = nullptr;

    // Screenshot state
    uint8_t* m_data = nullptr;
    size_t m_maxlen = 0;
    int m_offset = 0;
};

