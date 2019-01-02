

// for wifi and MQTT
#include <ESP8266WiFi.h>
#include <ESP8266HTTPClient.h>

// for the display
#include <Wire.h>
#include <LCD.h>
#include <LiquidCrystal_I2C.h>

// for MFRC522 RFID reader
#include <SPI.h>
#include <MFRC522.h>

// for hashing the card identifier (for privacy reasons, GDPR <3)
#include <sha256.h>

////// DEFINES //////

// LEDS
#define LED_BUILTIN 	2 	/* D4 */
#define LED_RED 		10	/* S3 */
#define LED_GREEN 		0	/* D3 */
#define BTN_PIN 		A0	/* A0 */

// Button definitions
#define NUM_BUTTONS		5
#define BTN_STEP_COUNT 	(1024.0 / NUM_BUTTONS)

// Update these with values suitable for your network, see wifi_settings.example.h
#include "wifi_settings.h"

// wifi and HTTP variables
WiFiClient espClient;
HTTPClient http;

// LCD variables
LiquidCrystal_I2C  lcd(0x27,2,1,0,4,5,6,7); // 0x27 is the I2C bus address for an unmodified backpack

// MFRC522 variables
#define RST_PIN 	16	/* D0 */
#define SS_PIN  	15	/* D8 */
 
MFRC522 rfid(SS_PIN, RST_PIN);
MFRC522::MIFARE_Key key;

// holds the current read card UID or card number
char card_buffer[64 + 1];

// sha256 related variables
// used internally for computing the hash
BYTE hash[SHA256_BLOCK_SIZE];

// the hashes of the scanned cards UID or card number
char sha256_buffer[2*SHA256_BLOCK_SIZE+1];
char sha256_buffer2[2*SHA256_BLOCK_SIZE+1];

// timers for working asynchronosly
#define PING_INTERVAL 60000
#define MESSAGE_TIMEOUT 6000
#define IDLE_TIMEOUT 10000
long timer_ping = 0;
long timer_timeout = 0;
long timer_idle = 0;



// for entering an action
#define MAX_ACTION_LENGTH 6
char enteredAction[MAX_ACTION_LENGTH + 1];
char currentActionIndex = 0;

// for buttons


typedef enum {
	STATE_IDLE,
	STATE_AWAITING_INPUT,
	STATE_AWAITING_INPUT_SECOND,
	STATE_SHOW_MESSAGE,
} t_states;


t_states current_state = STATE_IDLE;

void setup() {
	// intialize pins
	pinMode(LED_BUILTIN, OUTPUT);    
	pinMode(LED_RED, OUTPUT);
	pinMode(LED_GREEN, OUTPUT);

	pinMode(BTN_PIN, INPUT);

	// start serial debug port
	Serial.begin(115200);


	// initialize i2c LCD
	lcd.begin (20,4); 				// for 20 x 4 LCD module
	lcd.setBacklightPin(3,POSITIVE);
	lcd.setBacklight(HIGH);

	// connect to WiFi
	setup_wifi();
	
	// show connection status
	lcd.setCursor(0, 3);
	lcd.print("Connection: ");
	lcd.print(ping_backend() ? "Good" : "Error");
	

	// set up RFID
	SPI.begin(); // Init SPI bus
	rfid.PCD_Init(); // Init MFRC522 
  
	// Prepare the key (used both as key A and as key B)
	// using FFFFFFFFFFFFh which is the default at chip delivery from the factory
	for (byte i = 0; i < 6; i++) {
		key.keyByte[i] = 0xFF;
	}

	// wait before showing 
	timer_timeout = millis();
	timer_idle = millis();
	current_state = STATE_SHOW_MESSAGE;
}

void setup_wifi() {

  delay(10);
  // We start by connecting to a WiFi network
  //lcd.println();
  lcd.print("Connecting: ");
  lcd.setCursor(0, 1);
  lcd.print(ssid);

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
	digitalWrite(LED_BUILTIN, LOW);
	delay(100);    
	digitalWrite(LED_BUILTIN, HIGH);
	delay(100);
  }

  lcd.setCursor(0,2);
  lcd.print("IP:");
  lcd.print(WiFi.localIP());

}

int ping_backend(){
	// create connection
	http.begin(http_api_endpoint);
	http.addHeader("X-PING", "TRUE");

	// do get request
	int httpCode = http.GET();

	// close connection
	http.end();

	// check to make sure the request was successful
	return httpCode == HTTP_CODE_NO_CONTENT;

}

// sends the specific action and hashes to the backend, displaying the result on the screen
int send_backend_request(char* action, char* first_hash, char* second_hash){
	// connect to the backend
	http.begin(String(http_api_endpoint));

	// send data as headers
	http.addHeader("X-Action", String(action));
	http.addHeader("X-Card", String(first_hash));

	// add second hash if specified
	if(second_hash != nullptr){
		http.addHeader("X-Card2", String(second_hash));
	} 

	// issue GET request
	int httpCode = http.GET();

	// make sure we got a 200 OK back, otherwise display returned error code
	if (httpCode == HTTP_CODE_OK){
		String res = http.getString();
		display_message( res );
	}else{
		display_message("Backend error: " + String(httpCode) + "\n" + http.getString());
	}

	// close connection
	http.end();
}

void loop() {
	/*
	// backed ping loop always running
	long now = millis();
	if (timer_ping != 0 && (now - timer_ping) > PING_INTERVAL) {		
		timer_ping = now;

		// check to make sure server is still responding
		if(!ping_backend()){
			// No response!
			display_message("Backend not responding!");
		}
		
	}
	*/

	// idle timer always running as long as timer_ider != 0 (use this to turn off)
	long now = millis();
	if (timer_idle != 0 && (now - timer_idle) > IDLE_TIMEOUT) {
		
		// clear current action
		currentActionIndex = 0;

		// enter idle state here
		current_state = STATE_IDLE;
		lcd.off();
		timer_idle = 0;	// turn off timer
	}


	// different code based on state
	if(current_state == STATE_IDLE){
		// any button press leaves the idle mode
		if(button_clicked() > 0 || rfid.PICC_IsNewCardPresent()){
			//Serial.println("IDLE: LEAVE");
			lcd.on();

			// switch to input state and refresh last row of display in case a second card was never scanned
			current_state = STATE_AWAITING_INPUT;
			timer_idle = millis();
			print_current_action();
		}

		// wait more (probably not needed)
		//delay(100);

	} else if (current_state == STATE_SHOW_MESSAGE) {

		now = millis();
		if ((timer_timeout != 0 && (now - timer_timeout) > MESSAGE_TIMEOUT) || button_clicked() > 0 )  {		
			// stop timer
			timer_timeout = 0;

			lcdPrintString("Welcome! Select action and/or scan card!");
			
			// move to awaiting state and reset idle timer
			current_state = STATE_AWAITING_INPUT;
			timer_idle = millis();
			print_current_action();	
		}

	} else if (current_state == STATE_AWAITING_INPUT || current_state == STATE_AWAITING_INPUT_SECOND) {

		// was a button released
		int clicked_button = button_clicked();
		if (clicked_button > 0) {
			// yes! lastButtonState holds which one

			// reset idle timer
			timer_idle = millis();

			if(current_state == STATE_AWAITING_INPUT){

				// was this the "backspace" button?
				if (clicked_button == NUM_BUTTONS) {

					// yes, remove one icon
					currentActionIndex = max(0, currentActionIndex - 1);
					print_current_action();
				} else {
					if(currentActionIndex < MAX_ACTION_LENGTH)
						enteredAction[currentActionIndex++] = (clicked_button - 1) + '0';
					print_current_action();
				}

			} else {
				// waiting for second card, just abort and print current action again
				current_state = STATE_AWAITING_INPUT;
				print_current_action();
			}
		}

		// check if any card is present
		int card_length = scanForCard(card_buffer);
		if(card_length > 0){
			// reset idle timer
			timer_idle = millis();

			// is this the first card?
			if(current_state == STATE_AWAITING_INPUT){

				// add EOS to selected action and card hash
				enteredAction[currentActionIndex] = '\0';

				// hash card identity for privacy reasons
				hash_sha256_hex(card_buffer, card_length, sha256_buffer);

				// is this an action that requires two cards?
				if(currentActionIndex > 0 && enteredAction[0] == '2'){
					// yes, wait for another card to be scanned
					lcd.setCursor(0, 3);
					lcd.print("Scan second card...");
					current_state = STATE_AWAITING_INPUT_SECOND;
				} else {
					// send to server
					send_backend_request(enteredAction, sha256_buffer, nullptr);

					// clear current action
					currentActionIndex = 0;
				}
			} else {
				// this is the second scanned card!

				// hash card identity for privacy reasons
				hash_sha256_hex(card_buffer, card_length, sha256_buffer2);

				// send to server
				send_backend_request(enteredAction, sha256_buffer, sha256_buffer2);

				// clear current action
				currentActionIndex = 0;
			}
		}
	}

	// a small loop delay here makes the system more stable
	// otherwise it disconnects from AP and behaves unconsistently
	delay(10);
}

// checks for any button "clicks" by remembering the old pressed button and only returning >0 when button has been pressed AND released
int lastButtonState = 0;
int button_clicked(){
	int current_button = readButtons();
	int ret = -1;

	if (lastButtonState != 0 && current_button == 0) {
		ret = lastButtonState;
	}
	lastButtonState = current_button;

	return ret;
}

void print_current_action(){
	// start on last row
	lcd.setCursor(0, 3);
	lcd.print("Action:");

	// print entered characters
	int count;
	for(count = 0; count < currentActionIndex; count++){
		lcd.print(enteredAction[count]);
	}

	// underscore to indicate "current"
	lcd.print('_');
	count++;

	// fill with spaces to clear line
	for(;count < 20 - 7; count++){
		lcd.print(' ');
	}

}


// displays the specified message to the LCD, and starts the timer that returns after a specific timeout
void display_message(String msg){
	lcdPrintString(msg);

	// enter STATE_SHOW_MESSAGE
	current_state = STATE_SHOW_MESSAGE;
	timer_timeout = millis();
	timer_idle = 0;
}

// reads the buttons, returning wich button was pressed with 0 meaning no button
int readButtons(){
	// read adc
	int value = analogRead(BTN_PIN);

	// convert to pressed button and return
	return (int)( (float)value / BTN_STEP_COUNT + 1/2 );	
}

void lcdPrintString(String str){
	// https://coderwall.com/p/zfmwsg/arduino-string-to-char
	char *p = const_cast<char*>(str.c_str());

	lcdPrintString(p);
}
// shorthand for printing string without having to specify the length and offset
void lcdPrintString(char* str){
	lcdPrintString(str, 0, strlen(str));
}

// prints the provided string to the display by first clearing it. 
// Handles \n characters and implements correct line-wrapping. 
// Strings longer than 4*20=80 characters may have undefined/undesired side effects.
void lcdPrintString(char* str, unsigned int offset, unsigned int length){

	// clear screen and position pointer to 0,0
	lcd.clear();
	char row = 0;
	
	// iterate over recieved payload
	for (int i = 0, char_count = 0; i < length; i++) {
		
		if(str[i + offset] == '\n' || char_count >= 20){
			row++;
			char_count = 0;
			lcd.setCursor(0, row);
			
			// skip character if new line was recieved
			if(str[i + offset] == '\n') continue;
		}

		// print the character
		lcd.print((char)str[i + offset]);
		char_count++;
	}
}

// checks if a card is available. If it is, and is a Chalmers Student Union card the card number is returned in the buffer.
// Otherwise, the UID of the card is returned in the buffer. The return value is the number of bytes writen to the buffer,
// or -1 if no card was found. Requires a buffer with a length of at least 20 bytes (for cards with 10 byte UID, otherwise 18 is enough)
int scanForCard(char* buffer){
	// stores return value
	int returnedBytes = -1;

	// check if card was read (this also reads the UID of the card!)
	if(rfid.PICC_IsNewCardPresent() && rfid.PICC_ReadCardSerial()){

		// enable on-board LED
		digitalWrite(LED_BUILTIN, LOW);
		

		/*
		MFRC522::PICC_Type piccType = rfid.PICC_GetType(rfid.uid.sak);
		Serial.println(rfid.PICC_GetTypeName(piccType));

		// Check is the PICC of Classic MIFARE type
		if (piccType != MFRC522::PICC_TYPE_MIFARE_MINI &&  
			piccType != MFRC522::PICC_TYPE_MIFARE_1K &&
			piccType != MFRC522::PICC_TYPE_MIFARE_4K) {
			Serial.println(F("Your tag is not of type MIFARE Classic."));
			return 0;
		}
		*/
		
		// stores the returned status code
		MFRC522::StatusCode status;		
		
		//Serial.println(F("Authenticating using key A..."));

		// Authenticate using key A, this accesses the last block in each sector (the trailer block) - here block 3
		status = (MFRC522::StatusCode) rfid.PCD_Authenticate(MFRC522::PICC_CMD_MF_AUTH_KEY_A, 3, &key, &(rfid.uid));
		if (status == MFRC522::STATUS_OK) {
			// correctly authenticated
			
			// 18 bytes are required to read block (16 data bytes + 2 CRC check bytes)
			byte size = 16 + 2;

			// try to read data in block 1
			status = (MFRC522::StatusCode) rfid.MIFARE_Read(1, (byte*)buffer, &size);
			if (status == MFRC522::STATUS_OK) {
				// success!

				// check if this card is a valid Chalmers Student Union Card
				if(isNumeric(buffer, 16))
					returnedBytes = 16;				

			}
		}
		
		// if no valid card number was found, return HEX encoded UID instead
		if(returnedBytes < 0)
			returnedBytes =  array_to_string(rfid.uid.uidByte, rfid.uid.size, buffer);	

		// Halt PICC reading
		rfid.PICC_HaltA();

		// Stop encryption on PCD
		rfid.PCD_StopCrypto1();

		// card interaction done, turn off LED
		digitalWrite(LED_BUILTIN, HIGH);

	}
	

	// return 
	return returnedBytes;	
}

// checks if the provided data is a numeric value or not by only allowing ascii characters 0-9
// returns true if it is all numeric or false otherwise
bool isNumeric(char* buffer, uint8_t length ){
	for(uint8_t i = 0; i < length; i++){
		if(buffer[i] < '0' || buffer[i] > '9') return false;
	}

	return true;
}

/* Taken from https://stackoverflow.com/questions/44748740/convert-byte-array-in-hex-to-char-array-or-string-type-arduino*/
int array_to_string(byte array[], unsigned int len, char buffer[]){
    for (unsigned int i = 0; i < len; i++){
        byte nib1 = (array[i] >> 4) & 0x0F;
        byte nib2 = (array[i] >> 0) & 0x0F;
        buffer[i*2+0] = nib1  < 0xA ? '0' + nib1  : 'A' + nib1  - 0xA;
        buffer[i*2+1] = nib2  < 0xA ? '0' + nib2  : 'A' + nib2  - 0xA;
    }
    buffer[len*2] = '\0';

	return len*2;
}

// uses sha256 to hash the provided data, and puts the result in hex_out (length 2*SHA256_BLOCK_SIZE + 1 = 65 )
void hash_sha256_hex(char* data, int length, char* hex_out){
	Sha256* sha256Instance = new Sha256();

	sha256Instance->update((byte*)data, length);
	sha256Instance->final(hash);

	for(int i=0; i<SHA256_BLOCK_SIZE; ++i)
		sprintf(hex_out+2*i, "%02X", hash[i]);
}
