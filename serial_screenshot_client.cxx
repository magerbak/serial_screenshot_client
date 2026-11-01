#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <termios.h>
#include <fcntl.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <sys/select.h>

#define STB_IMAGE_WRITE_IMPLEMENTATION
#include "stb_image_write.h"

class ScreenshotClient
{
public:
    ScreenshotClient() = default;
    ~ScreenshotClient();

    bool capture(const char* dev, const char* filename);

private:
    static constexpr uint8_t VERSION = 1;
    static constexpr size_t HDR_LEN = 11;
    enum State {
        STATE_INIT,
        STATE_ECHO,
        STATE_HDR,
        STATE_IMG,
        STATE_PROCESSING,
        STATE_COMPLETE,
    };
    bool openSerial(const char* dev, unsigned int baudrate);
    bool sendCommand(const char* cmd, size_t len);
    bool parseHeader(const uint8_t* data);

    int m_dev = -1;

    State m_state = STATE_INIT;
    bool m_verbose = true;

    int m_width = 0;
    int m_height = 0;
    uint8_t* m_image = nullptr;
    int m_offset = 0;

    time_t m_start = 0;
};

ScreenshotClient::~ScreenshotClient()
{
    delete[] m_image;
    m_image = nullptr;

    if (m_dev != -1) {
        close(m_dev);
        m_dev = -1;
    }
}

bool ScreenshotClient::capture(const char* dev, const char* filename)
{
    uint8_t data[512];
    size_t offset = 0;
    bool ok = true;
    int rc = 0;
    int n;
    static unsigned int count = 0;

    if (!openSerial(dev, /* 115200 */ 921600)) {
        return false;
    }

    while (ok && m_state != STATE_COMPLETE) {
        switch (m_state) {
        case STATE_INIT:
            // Send a screenshot command
            if (!sendCommand("S", 1)) {
                fprintf(stderr, "Error sending screenshot command\n");
                ok = false;
            }
            else {
                offset = 0;
                m_state = STATE_HDR;
            }
            break;
        case STATE_ECHO:
            rc = read(m_dev, &data[0], 1);
            if (rc < 0 && errno != EAGAIN) {
                fprintf(stderr, "Error reading\n");
                ok = false;
                break;
            }
            else if (rc > 0) {
                putchar(data[0]);
            }
            break;
        case STATE_HDR:
            // Header is 11 bytes long:
            // V1WxxHxxYxD
            rc = read(m_dev, &data[offset], HDR_LEN - offset);
            if (rc < 0 && errno != EAGAIN) {
                fprintf(stderr, "Error reading header\n");
                ok = false;
                break;
            }
            else if (rc > 0) {
                offset += rc;
                if (offset == HDR_LEN) {
                    if (!parseHeader(data)) {
                        ok = false;
                        break;
                    }
                    printf("Receiving image %dx%d\n", m_width, m_height);
                    offset = 0;
                    m_start = time(nullptr);
                    m_state = STATE_IMG;
                }
            }
            break;
        case STATE_IMG:
            n = m_width * m_height * 3 - m_offset;
            rc = read(m_dev, &m_image[m_offset], n);
            if (rc < 0 && errno != EAGAIN) {
                fprintf(stderr, "Error reading header\n");
                ok = false;
                break;
            }
            else if (rc > 0) {
                m_offset += rc;
                count++;
                if (count % 32 == 0) {
                    printf(".");
                    if (count > 7680 / 8) {
                        printf(" %d bytes\n", m_offset);
                        count = 0;
                    }
                    fflush(stdout);
                }
                if (m_offset == m_width * m_height * 3) {
                    printf(" %d bytes\n", m_offset);
                    m_state = STATE_PROCESSING;
                }
            }
            break;
        case STATE_PROCESSING:
            if (!stbi_write_png(filename, m_width, m_height, 3, m_image, m_width * 3)) {
                fprintf(stderr, "Failed to write output file\n");
                ok = false;
            }
            else {
                time_t now = time(nullptr);
                printf("Wrote %s %.0fKB/s\n", filename, (float)m_offset / (now - m_start) / 1024.0);
                m_state = STATE_COMPLETE;
            }
            break;
        case STATE_COMPLETE:
            break;
        }

    }

    if (m_state < STATE_PROCESSING) {
        sendCommand("X", 1);
        sleep(1);
    }

    return ok && m_state == STATE_COMPLETE;
}

bool ScreenshotClient::openSerial(const char* dev, unsigned int baudrate)
{
    // Open serial port using baudrate and 8N1.
    struct termios attr;
    m_dev = open(dev, O_RDWR | O_NOCTTY | O_NONBLOCK);
    if (m_dev < 0) {
        fprintf(stderr, "Error opening input device (%s)\n", strerror(errno));
        return false;
    }

    memset(&attr, 0, sizeof(attr));
    if (cfsetspeed(&attr, baudrate) < 0) {
        fprintf(stderr, "Error setting baudrate (%s)\n", strerror(errno));
        return false;
    }

    // Enable input, ignore modem controls lines, 8-bit characters.
    attr.c_cflag |= CS8 | CLOCAL | CREAD;
    // No parity
    attr.c_iflag |= IGNPAR;
    // Min number of characters to read
    attr.c_cc[VMIN] = 1;
    // No timeout (we're non-blocking)
    attr.c_cc[VTIME] = 0;
    if (tcsetattr(m_dev, TCSANOW, &attr) < 0) {
        fprintf(stderr, "Error setting serial attributes (%s)\n", strerror(errno));
        return false;
    }

    // Flush any previously buffered data
    tcflush(m_dev, TCIFLUSH);
    return true;
}

bool ScreenshotClient::sendCommand(const char* cmd, size_t len)
{
    int rc = write(m_dev, cmd, len);
    return rc == (int)len;
}

bool ScreenshotClient::parseHeader(const uint8_t* data)
{
    if (data[0] != 'V' && data[1] != VERSION) {
        fprintf(stderr, "Unsupported version (%d)\n", data[1]);
        return false;
    }
    if (data[2] != 'W' && data[5] != 'H' && data[8] != 'Y' && data[10] != 'D') {
        fprintf(stderr, "Invalid header format\n");
        return false;
    }
    if (data[9] != 24) {
        fprintf(stderr, "Unsupported color depth (%d)\n", data[9]);
        return false;
    }

    m_width = data[3] << 8 | data[4];
    m_height = data[6] << 8 | data[7];
    m_image = new uint8_t[m_width * m_height * 3];
    return true;
}

int main(int argc, char** argv)
{
    ScreenshotClient ss;

    if (argc != 3) {
        printf("Serial screen capture client. v1.0\n");
        printf("See example TFTServer implementation to run on embedded device\n");
        printf("Usage: %s <serialdev> <filename>\n", argv[0]);
        printf("\nFor example: %s /dev/ttyACM0 screenshot.png\n", argv[0]);
        return 255;
    }

    bool ok = ss.capture(argv[1], argv[2]);

    return ok ? 0 : 255;
}
