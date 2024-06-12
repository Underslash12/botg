//

#include <Arduino.h>
#include "driver/i2s.h"
#include <HttpClient.h>
#include <WiFi.h>
#include <Wire.h>
#include <inttypes.h>
#include <stdio.h>
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"
#include <TFT_eSPI.h> // Hardware-specific library
#include <SPI.h>
#include <RotaryEncoder.h>
#include <TinyGPSPlus.h>
#include <SoftwareSerial.h>

#include "utils.h"
#include "customBase64.h"

const i2s_port_t I2S_PORT = I2S_NUM_0;

// mic pins
#define BCLK_PIN 32
#define LRCL_PIN 25
#define DOUT_PIN 33
#define SEL_PIN 27
// button pin
#define BTN_PIN 26
// encoder pins
#define ENC_A_PIN 39
#define ENC_B_PIN 38
// gps buttons
#define GPS_TX_PIN 12
#define GPS_RX_PIN 13

// how long it should take to consider it a long press
#define LONG_PRESS_MS 1500

// create the display
TFT_eSPI tft = TFT_eSPI();
// rotary encoder def
RotaryEncoder encoder(ENC_B_PIN, ENC_A_PIN, RotaryEncoder::LatchMode::TWO03);

// gps transfer rate over software serial
#define GPS_BAUD 4800
// gps objects
TinyGPSPlus gps;
SoftwareSerial ss(GPS_RX_PIN, GPS_TX_PIN);

// buffers for wifi credentials
char ssid[50];
char pass[50];

// network constants
#define NETWORK_TIMEOUT 5000
#define NETWORK_DELAY 5000

#define HTTP_ENDPOINT "54.153.115.196"
#define HTTP_PORT 5000

// the sample rate of the SPH0645 in hz
const size_t SAMPLE_RATE = 22050;
// size of the partition sectors
// for the sake of convienience, I'm going to align the buffers to this
const size_t SECTOR_SIZE = 4096;
// the double buffer which holds the 16-bit raw data
const size_t BUFFER_COUNT = 2;
typedef int16_t sample_t;
sample_t* buffers[BUFFER_COUNT];
constexpr size_t BUFFER_LEN = SECTOR_SIZE * 8;
constexpr size_t BUFFER_SIZE = BUFFER_LEN * sizeof(sample_t); 
int current_buf = 0;
int current_buf_pos = 0;
// amount of bytes to send in each http request
constexpr size_t DATA_SEND_SIZE = SECTOR_SIZE / 2;

// buffer which stores the query as its built
char* query_buffer;
constexpr size_t QUERY_BUFFER_SIZE = SECTOR_SIZE;

// initialize nvs
void init_nvs() {
    // Initialize NVS
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES ||
        err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        // NVS partition was truncated and needs to be erased
        // Retry nvs_flash_init
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    ESP_ERROR_CHECK(err);
}

// store the given ssid and pass in nvs
void store_ssid_pass(char* ssid, size_t ssid_size, char* pass, size_t pass_size) {
	// error value
	esp_err_t err;

	// get the nvs handle
    nvs_handle_t my_handle;
    err = nvs_open("wifi_creds", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } 
	else {
        err = nvs_set_str(my_handle, "ssid", ssid);
        err |= nvs_set_str(my_handle, "pass", pass);
		switch (err) {
            case ESP_OK:
                Serial.printf("Loaded ssid and pass in nvs\n");
                break;
            default:
                Serial.printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
	}

	// Close
    nvs_close(my_handle);
}

void load_ssid_pass() {
	// error value
	esp_err_t err;

	// get the nvs handle
    nvs_handle_t my_handle;
    err = nvs_open("wifi_creds", NVS_READWRITE, &my_handle);
    if (err != ESP_OK) {
        Serial.printf("Error (%s) opening NVS handle!\n", esp_err_to_name(err));
    } 
	else {
        size_t ssid_len = 50;
        size_t pass_len = 50;
        err = nvs_get_str(my_handle, "ssid", ssid, &ssid_len);
        err |= nvs_get_str(my_handle, "pass", pass, &pass_len);
        switch (err) {
            case ESP_OK:
                Serial.printf("Loaded ssid and pass\n");
                break;
            case ESP_ERR_NVS_NOT_FOUND:
                Serial.printf("ssid or pass do not exist in nvs\n");
                break;
            default:
                Serial.printf("Error (%s) reading!\n", esp_err_to_name(err));
        }
    }
    // Close
    nvs_close(my_handle);
}

// initialize the i2s device
void init_i2s() {
	esp_err_t err;

	// The I2S config as per the example
	const i2s_config_t i2s_config = {
		.mode = i2s_mode_t(I2S_MODE_MASTER | I2S_MODE_RX), // Receive, not transfer
		.sample_rate = 22050,                         // 16KHz
		.bits_per_sample = I2S_BITS_PER_SAMPLE_32BIT, // could only get it to work with 32bits
		.channel_format = I2S_CHANNEL_FMT_ONLY_RIGHT, // use right channel
		.communication_format = i2s_comm_format_t(I2S_COMM_FORMAT_I2S | I2S_COMM_FORMAT_I2S_MSB),
		.intr_alloc_flags = ESP_INTR_FLAG_LEVEL1,     // Interrupt level 1
		.dma_buf_count = 4,                           // number of buffers
		.dma_buf_len = 8                              // 8 samples per buffer (minimum)
	};

	// The pin config as per the setup
	const i2s_pin_config_t pin_config = {
		.bck_io_num = BCLK_PIN,   // Serial Clock (SCK)
		.ws_io_num = LRCL_PIN,    // Word Select (WS)
		.data_out_num = I2S_PIN_NO_CHANGE, // not used (only for speakers)
		.data_in_num = DOUT_PIN   // Serial Data (SD)
	};

	// Configuring the I2S driver and pins.
	// This function must be called before any I2S driver read/write operations.
	err = i2s_driver_install(I2S_PORT, &i2s_config, 0, NULL);
	if (err != ESP_OK) {
		Serial.printf("Failed installing driver: %d\n", err);
		while (true);
	}
	err = i2s_set_pin(I2S_PORT, &pin_config);
	if (err != ESP_OK) {
		Serial.printf("Failed setting pin: %d\n", err);
		while (true);
	}
	Serial.println("I2S driver installed.");
}

// initialize the buffers and return if it was successful
bool init_buffers() {
    for (int i = 0; i < BUFFER_COUNT; i++) {
        buffers[i] = (sample_t*) malloc(BUFFER_SIZE);
        if (buffers[i] == NULL) {
            Serial.print("Couldn't initialize buffer[");
			Serial.print(i);
			Serial.println("]");
            return false;
        }
    }

	query_buffer = (char*) malloc(QUERY_BUFFER_SIZE);
	if (query_buffer == NULL) {
		Serial.print("Couldn't initialize query buffer");
		return false;
	}

    return true;
}

void init_wifi() {
	// start wifi	
	WiFi.begin(ssid, pass);
	char disp[5] = "\\|/-";
	int dispIndex = 0;
    while (WiFi.status() != WL_CONNECTED) {
        delay(500);
        Serial.print(".");
		// visual display of progress
		tft.drawChar(disp[dispIndex], 10, 30);
		dispIndex++;
		if (dispIndex >= 4) {
			dispIndex = 0;
		}
    }
	// delay(1000);
	Serial.println("Connected");
	tft.drawString("Connected", 10, 30);
	delay(1000);
}

// check against low since the BTN_PIN is configured as INPUT_PULLUP
bool button_pressed() {
	return digitalRead(BTN_PIN) == LOW;
}

// wait for the button to be pressed, returning true if it 
// was a long press, false if it was a short press
// short_circ controls whether or not the long press has to be released to return
bool wait_for_button_press(bool short_circ = false) {
	// wait for button press
	while (!button_pressed()) {
		delay(10);
	}
	long start_time = millis();

	// wait for button release (or long press)
	while (button_pressed()) {
		if (short_circ) {
			if ((millis() - start_time) >= LONG_PRESS_MS) {
				return true;
			}
		}
		delay(10);
	}

	// another check here in case if it was released shortly after LONG_PRESS_MS
	return (millis() - start_time) >= LONG_PRESS_MS;
}

// initialize the ttgo display
void init_tft() {
	tft.init();
	tft.setRotation(3);
	tft.fillScreen(TFT_BLACK);

	tft.setTextSize(2);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);
}

// draw the keyboard with the highlighted character being highlighted
// only has support for ASCII, space, and del key
// highlighted == 0 is the shift key, 1 is space, 2 is del, 3 is save
void draw_keyboard(char highlighted, bool shift, char** rows) {
	tft.fillScreen(TFT_BLACK);
	tft.setTextSize(2);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);

	// offset the keyboard
	int off_x = 20;
	int off_y = 50;
	
	// draw the main keyboard
	int row = 0;
	int col = 0;
	char c;
	while (rows[row] != NULL) {
		c = rows[row][col];
		while (c != '\0') {
			// highlight the selected character
			if (c == highlighted) tft.setTextColor(TFT_BLACK, TFT_WHITE, true);

			// draw the character depending on whether shift is on
			if (shift) {
				tft.drawChar(c, 15 * col + row * 8 + off_x, 20 * row + off_y);
			}
			else {
				tft.drawChar(c + 32, 15 * col + row * 8 + off_x, 20 * row + off_y);
			}

			// unhighlight the character
			if (c == highlighted) tft.setTextColor(TFT_WHITE, TFT_BLACK, false);
			col++;
			c = rows[row][col];
		}
		col = 0;
		row++;
	}

	// draw shift
	if (highlighted == 0) tft.setTextColor(TFT_BLACK, TFT_WHITE, true);
	tft.drawString("^s", off_x, 60 + off_y);
	if (highlighted == 0) tft.setTextColor(TFT_WHITE, TFT_BLACK, false);

	// draw space
	if (highlighted == 1) tft.setTextColor(TFT_BLACK, TFT_WHITE, true);
	tft.drawString("[   ]", 38 + off_x, 60 + off_y);
	if (highlighted == 1) tft.setTextColor(TFT_WHITE, TFT_BLACK, false);

	// draw del
	if (highlighted == 2) tft.setTextColor(TFT_BLACK, TFT_WHITE, true);
	tft.drawString("del", 110 + off_x, 60 + off_y);
	if (highlighted == 2) tft.setTextColor(TFT_WHITE, TFT_BLACK, false);

	// draw save
	if (highlighted == 3) tft.drawRect(155 + off_x, 12 + off_y, 50, 50, TFT_WHITE);
	tft.drawString("/", 188 + off_x, 20 + off_y);
	tft.drawString("\\/", 160 + off_x, 36 + off_y);
}

void draw_textbox(char* current, int max_len) {
	// offset the keyboard
	int off_x = 20;
	int off_y = 10;

	tft.drawString(current, off_x, off_y);
}

// use the on screen keyboard to read one string
void enter_str(char* dst, int dst_len, char** board, int* lens) {
	tft.fillScreen(TFT_BLACK);
	tft.setTextSize(2);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);

	// keep track of where we are on the keyboard
	int row = 0;
	int col = 0;
	int dstIndex = 0;
	bool shifted = false;
	bool longDelay = false;

	// keep track of encoder status
	int prevPos = 0, newPos;
	bool changed;
	draw_keyboard('q', shifted, board);
	draw_textbox(dst, dst_len);

	while (true) {
		// tick the encoder
		encoder.tick();
		newPos = encoder.getPosition();
		// track whether or not the screen needs to be redrawn
		changed = false;
		// long delay after a press
		longDelay = false;

		// if the encoder changed position
		if (newPos != prevPos) {
			// clockwise
			if (newPos - prevPos == 1) {
				if (!(row == 3 && col == 3)) {
					col++;
					if (col >= lens[row]) {
						row++;
						col = 0;
					}
					changed = true;
				}
			}
			// ccw
			else if (newPos - prevPos == -1) {
				if (!(row == 0 && col == 0)) {
					col--;
					if (col < 0) {
						row--;
						col = lens[row] - 1;
					}
					changed = true;
				}
			}

			prevPos = newPos;
		}

		// if the button is being pressed
		if (button_pressed()) {
			// if the user pressed a character
			if (row != 3) {
				// make sure it isn't too long
				if (dstIndex < dst_len) {
					if (shifted) {
						dst[dstIndex] = board[row][col];
					}
					else {
						dst[dstIndex] = board[row][col] + 32;
					}
					dstIndex++;
					changed = true;
				}
			}
			// otherwise if the user pressed a special key
			else {
				if (col == 0) {
					shifted = !shifted;
					changed = true;
				}
				else if (col == 1) {
					if (dstIndex < dst_len) {
						dst[dstIndex] = ' ';
						dstIndex++;
						changed = true;
					}
				}
				else if (col == 2) {
					if (dstIndex > 0) {
						dst[dstIndex-1] = '\0';
						dstIndex--;
						changed = true;
					}
				}
				else if (col == 3) {
					return;
				}
			}
			longDelay = true;
		}

		// redraw if there were any changes
		if (changed) {
			if (row != 3) {
				draw_keyboard(board[row][col], shifted, board);
				draw_textbox(dst, dst_len);
			}
			else {
				draw_keyboard(col, shifted, board);
				draw_textbox(dst, dst_len);
			}
		}

		// give the encoder a bit of time to breathe
		delay(10);
		if (longDelay) {
			delay(300);
		}
	}
}

// create an on-screen keyboard controllable with the rotary encoder
void enter_wifi_creds() {
	tft.setTextSize(2);
	tft.setTextColor(TFT_WHITE, TFT_BLACK);

	// setup board
	char* board[4];
	board[0] = "QWERTYUIOP";
	board[1] = "ASDFGHJKL";
	board[2] = "ZXCVBNM";
	board[3] = NULL;
	// setup lengths
	int lens[4] = {10, 9, 7, 4};

	// ssid
	int temp_ssid_len = 16; 
	char temp_ssid[temp_ssid_len];
	for (int i = 0; i < temp_ssid_len; i++) {
		temp_ssid[i] = 0;
	}
	tft.fillScreen(TFT_BLACK);
	tft.drawString("SSID:", 10, 10);
	delay(500);
	enter_str(temp_ssid, temp_ssid_len, board, lens);

	// pass
	tft.fillScreen(TFT_BLACK);
	tft.drawString("PASS:", 10, 10);
	delay(500);
	int temp_pass_len = 16; 
	char temp_pass[temp_pass_len];
	for (int i = 0; i < temp_pass_len; i++) {
		temp_pass[i] = 0;
	}
	enter_str(temp_pass, temp_pass_len, board, lens);
	delay(500);

	// store the temp credentials in nvs
	store_ssid_pass(temp_ssid, temp_ssid_len, temp_pass, temp_pass_len);
}

// 
void setup() {
	// initialize serial
	Serial.begin(115200);
	ss.begin(GPS_BAUD);
	Wire.begin();

	pinMode(BTN_PIN, INPUT_PULLUP);

	// initialize the i2s mic
	init_i2s();
	init_buffers();

	// prompt user to long press or short press
	// short press loads the wifi credentials from nvs
	// long press loads an on-screen keyboard to input it
	init_tft();
	tft.drawString("Initializing WiFi:", 10, 10);
	tft.drawString("Short press to use", 10, 40);
	tft.drawString("stored ssid & pass", 10, 60);
	tft.drawString("Long press to input", 10, 80);
	tft.drawString("new ssid & pass", 10, 100);

	init_nvs();
	// store_ssid_pass("iPhone", 6, "here we go", 10);
	// load_ssid_pass();

	bool long_press = wait_for_button_press(false);
	if (long_press) {
		enter_wifi_creds();
	}
	// load the credentials from nvs to ram
	load_ssid_pass();
	Serial.printf("ssid: %s\n", ssid);
	Serial.printf("pass: %s\n", pass);

	tft.fillScreen(TFT_BLACK);
	tft.drawString("Connecting to Wifi:", 10, 10);
	init_wifi();
}

// send an http request with the specified path and query
// returns 0 if it was successful, otherwise it errored
int http_get(char* path_query) {
	WiFiClient c;
    HttpClient http(c);
    
	int err = 0;
	err = http.get(HTTP_ENDPOINT, HTTP_PORT, path_query);
	if (err != 0) { 
		Serial.print("Http response failed with error code ");
		Serial.println(err);
		return err;
	}

	return 0;
}

// get values from gps
const double CURR_LAT = 33.68805530124304;
const double CURR_LON = -117.83067461174788;
void get_lat_lon(double& lat, double& lon) {
	lat = gps.location.lat();
	if (!gps.location.isValid()) lat = CURR_LAT;
	lon = gps.location.lng();
	if (!gps.location.isValid()) lon = CURR_LON;
}

// send data in the partition to the cloud server via http
void send_audio_data() {
	// some extra display
	tft.fillScreen(TFT_BLACK);
	tft.drawString("Sending audio data:", 10, 10);

    // set the current state
    // state = STATE_SENDING_DATA;
    size_t src_offset;

	// loop over every buffer starting at the current one and write its data to the cloud
	// should start at the next DATA_SEND_SIZE chunk over from the current position so that
	// the final bits of audio that might be out of order are at the end, rather than the beginning
    int buffer_index = current_buf;
    for (int i = 0; i < BUFFER_COUNT; i++) {
        for (int j = 0; j < BUFFER_SIZE / DATA_SEND_SIZE; j++) {
            // format pending_buf to hold query information
			// query_buffer[0] = '/';
			// query_buffer[1] = '?';
            // query_buffer[2] = 'i';
            // query_buffer[3] = '=';
            // int i_len = write_num_to_str(query_buffer + 4, i * (BUFFER_SIZE / DATA_SEND_SIZE) + j);
            // query_buffer[4 + i_len + 0] = '&';
            // query_buffer[4 + i_len + 1] = 't';
            // query_buffer[4 + i_len + 2] = '=';
            // int t_len = write_num_to_str(query_buffer + 4 + i_len + 3, BUFFER_COUNT * (BUFFER_SIZE / DATA_SEND_SIZE));
            // query_buffer[4 + i_len + 3 + t_len + 0] = '&';

			// even if the gps is indoors, the fallback values are still good
			double lat;
			double lon;
			get_lat_lon(lat, lon);

			// snprintf(query_buffer + (4 + i_len + 3 + t_len + 1), QUERY_BUFFER_SIZE - 20, "%f", )

            // query_buffer[4 + i_len + 3 + t_len + 1] = 'd';
            // query_buffer[4 + i_len + 3 + t_len + 2] = '=';
            // int data_offset = 4 + i_len + 3 + t_len + 3;

			
			int data_offset = snprintf(query_buffer, QUERY_BUFFER_SIZE, "/?i=%d&t=%d&lat=%f&lon=%f&d=", 
				i * (BUFFER_SIZE / DATA_SEND_SIZE) + j,
				BUFFER_COUNT * (BUFFER_SIZE / DATA_SEND_SIZE),
				lat,
				lon
			);
			for (int dec_correct = 0; dec_correct < data_offset; dec_correct++) {
				if (query_buffer[dec_correct] == '.') {
					query_buffer[dec_correct] = '_';
				}
			}

            // convert the read data into base64 and store it after the query data in pending_buf
            size_t out_len;
			// this is just the next
            src_offset = (BUFFER_SIZE / DATA_SEND_SIZE) * j;
            base64_encode_url(((unsigned char*) buffers[buffer_index]) + src_offset, DATA_SEND_SIZE, ((unsigned char*) query_buffer) + data_offset, out_len);
            query_buffer[data_offset + out_len] = '\0';

			// send an http request with the specified buffer
			http_get(query_buffer);

			// give time for the watchdog to be satisfied
			// not sure if this is necessary in the arduino version
            delay(10);

			// draw progress
			if (j % 1 == 0) {
				tft.drawString("                ", 10, 30);
				int draw_len = tft.drawNumber(i * (BUFFER_SIZE / DATA_SEND_SIZE) + j + 1, 10, 30);
				draw_len += tft.drawChar('/', draw_len + 20, 30);
				draw_len += 20;
				tft.drawNumber(BUFFER_COUNT * (BUFFER_SIZE / DATA_SEND_SIZE), draw_len + 10, 30);
			}
        }
        
        buffer_index++;
        if (buffer_index >= BUFFER_COUNT) {
            buffer_index = 0;
        }
    }

	// draw final progress
	tft.drawString("                ", 10, 30);
	int draw_len = tft.drawNumber(BUFFER_COUNT * (BUFFER_SIZE / DATA_SEND_SIZE), 10, 30);
	draw_len += tft.drawChar('/', draw_len + 20, 30);
	draw_len += 20;
	tft.drawNumber(BUFFER_COUNT * (BUFFER_SIZE / DATA_SEND_SIZE), draw_len + 10, 30);
	tft.drawString("Audio data sent", 10, 50);
}

bool displayAudioInfo = true;

void loop() {
	if (displayAudioInfo) {
		// display some info
		tft.fillScreen(TFT_BLACK);
		tft.drawString("Recording audio", 10, 10);
		tft.drawString("Press to save", 10, 30);
		displayAudioInfo = false;
	}
	
	// Read a single sample and log it for the Serial Plotter.
	int32_t sample = 0;
	size_t bytes_read;
	esp_err_t err = i2s_read(I2S_PORT, (char*) &sample, 4, &bytes_read, portMAX_DELAY); // no timeout

	// since the sample
	// sample = ((sample >> 1) & 0x7fffffff) | ((sample & 0x1) << 31);
	sample = (sample >> 16) & 0xff;

	// place the sample in the next available position in the buffers
	// goes through each buffer before going back to the beginning
	// need to have multiple buffers instead of a single one because the heap is fragmented
	buffers[current_buf][current_buf_pos] = (sample_t) sample;
	current_buf_pos++;
	if (current_buf_pos >= BUFFER_LEN) {
		current_buf_pos = 0;
		current_buf++;
		if (current_buf >= BUFFER_COUNT) {
			current_buf = 0;
		}
	}

	// check if the button is pressed (not too fast)
	if (current_buf_pos % DATA_SEND_SIZE == 0) {
		// load gps data occassionally, only read a few at a time so it doesn't stall the audio 
		int encoded = 0;
		while (ss.available() && encoded < 4) {
			gps.encode(ss.read());
			encoded++;
		}

		if (button_pressed()) {
			send_audio_data();
			delay(1000);
			displayAudioInfo = true;
		}	
	}
}