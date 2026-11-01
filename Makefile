all: serial_screenshot_client

serial_screenshot_client: serial_screenshot_client.cxx
	g++ -o $@ -Wall -O2 $^

clean:
	rm serial_screenshot_client

