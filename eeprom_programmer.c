/* 2024 EEPROM Programmer.
   Written in C by Bob Szymanski

   This is a simple EEPROM programmer designed for the 39SF0X0(A)
   EEPROM chips made by SST. To test this, I am using the largest of
   the bunch, the 39SF040, which is 512KB in capacity.

   The 39SF0X0 chips are programmed byte-by-byte, and there are 
   3 byte sequences needed prior to performing a write.
   There is also a 6-Byte full chip erase sequence that we perform, 
   as according to the documentation, a sector must be erased prior
   to writing... and it's much easier to just erase the chip than 
   perform a looped sector erase.

   The 39SF0X0 series chips are 5V logic only, so we tap into the 5V
   rail on pin 40 of the Pi Pico, as this is designed to be powered
   by the micro USB connector on the Pi. I have supplied that 5V line to
   the shift registers VCC pins, the EEPROM VCC line, and to two 
   TXS0108E 8 pin logic level converters. The first of which is used to 
   drive the 8 data pins to and from the EEPROM, and 3 pins of the second
   module are used to drive the /CE, /WE, and /OE signals.
   I have found that the 74HC595N chips that I am using are working perfectly
   with VCC being 5V and the logic input signals being at 3.3v (Latch, Data, Clock),
   so those lines can be driven directly from the Pi Pico.

   The SD card operations are performed by importing no-OS-FatFS-SD-SPI-RPi-Pico
   basically as-is, and just running the commands supplied in their documentation
   and their examples. One thing to note, if you are coming from the DigiKey guide for 
   using this lib, the wiring diagram is wrong... Refer to the actual documentation 
   at https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico or see below for pinout.

   DISCLAIMER: You are free to use this code in any manner you see fit, although
   I cannot guarantee it will work perfectly for you or that it cannot potentially
   cause damage to any hardware. But it works for me! Use at your own risk.
*/

#include <stdint.h>
#include <string.h>
#include "hardware/i2c.h"
#include "pico/stdlib.h"
#include "stdio.h"
#include "./lib/ssd1306/ssd1306.h" // OLED lib:
#include "ff.h" // SD card lib
#include "sd_card.h" // SD card lib

// Shift register pins:
const int DATA_PIN_NUMBER = 2;
const int LATCH_PIN_NUMBER = 3;
const int CLOCK_PIN_NUMBER = 4;
const int ADDRESS_LINES = 24; // Rename.. this is 8 * number of shift registers

// ROM data:
const int MAX_EEPROM_ADDRESS_SPACE = 524288; // 2 ^ 19 : we have 19 address lines A0 -> A18

// EEPROM Pins:
const int WRITE_ENABLE_PIN = 28;
const int OUTPUT_ENABLE_PIN = 27;
const int CHIP_ENABLE_PIN = 26;
const int D0_PIN = 8;
const int D1_PIN = 9;
const int D2_PIN = 10;
const int D3_PIN = 11;
const int D4_PIN = 12;
const int D5_PIN = 13;
const int D6_PIN = 14;
const int D7_PIN = 15;  // Why is 15 labeled as DO_NOT_USE ??

// Misc Pins:
const int ONBOARD_LED_PIN = 25;

// OLED Params:
const uint OLED_I2C_DATA_PIN = 0;
const uint OLED_I2C_CLK_PIN = 1;
i2c_inst_t *OLED_I2C_PORT = i2c0;
const uint OLED_I2C_BAUD = 400000;
const uint16_t OLED_PX_WIDTH = 128;
const uint16_t OLED_PX_HEIGHT = 64;
const uint8_t OLED_I2C_ADDRESS = 0x3C;
const uint32_t OLED_TEXT_SCALE = 1;
ssd1306_t _display;

/* SD CARD: See https://github.com/carlk3/no-OS-FatFS-SD-SPI-RPi-Pico?tab=readme-ov-file
 Physical Pin 24 (GPIO 18): CLOCK
 Physical Pin 21 (GPIO 16): MISO
 Physical Pin 25 (GPIO 19): MOSI
 Physical Pin 22 (GPIO 17): CS
 */

// This int is used to perform a nop
int dummy = 0;

// nop() : same idea as a NOP assembly instruction (but slightly longer)
// TODO: this could use a lot of improvement... see readme on github.
void nop() {
  dummy += 1;
  for (int i = 0; i < 200; i++) {
    dummy += 1;
  }
}
 
/// @brief setup() is essentially following the Arduino pattern.
///        The main() function should first call setup, then loop() the main app logic.
void setup() {
  stdio_init_all();

  // Onboard LED:
  gpio_init(ONBOARD_LED_PIN);
  gpio_set_dir(ONBOARD_LED_PIN, GPIO_OUT);

  // Shift registers:
  gpio_init(LATCH_PIN_NUMBER);
  gpio_set_dir(LATCH_PIN_NUMBER, GPIO_OUT);
  gpio_init(DATA_PIN_NUMBER);
  gpio_set_dir(DATA_PIN_NUMBER, GPIO_OUT);
  gpio_init(CLOCK_PIN_NUMBER);
  gpio_set_dir(CLOCK_PIN_NUMBER, GPIO_OUT);

  // OLED stuff:
  gpio_set_function(OLED_I2C_CLK_PIN, GPIO_FUNC_I2C);
  gpio_set_function(OLED_I2C_DATA_PIN, GPIO_FUNC_I2C);
  gpio_pull_up(OLED_I2C_CLK_PIN);
  gpio_pull_up(OLED_I2C_DATA_PIN);
  i2c_init(OLED_I2C_PORT, OLED_I2C_BAUD);
  _display.external_vcc = false;
  ssd1306_init(&_display, OLED_PX_WIDTH, OLED_PX_HEIGHT, OLED_I2C_ADDRESS,
               OLED_I2C_PORT);
  ssd1306_clear(&_display);

  // Set SRAM or EEPROM pins:
  gpio_init(CHIP_ENABLE_PIN);
  gpio_set_dir(CHIP_ENABLE_PIN, GPIO_OUT);
  gpio_init(WRITE_ENABLE_PIN);
  gpio_set_dir(WRITE_ENABLE_PIN, GPIO_OUT);
  gpio_init(OUTPUT_ENABLE_PIN);
  gpio_set_dir(OUTPUT_ENABLE_PIN, GPIO_OUT);

  // Put SRAM or EEPROM in chip enable, but not reading or writing state.
  gpio_put(WRITE_ENABLE_PIN, true);
  gpio_put(OUTPUT_ENABLE_PIN, true);
  gpio_put(CHIP_ENABLE_PIN, false);

  gpio_init(D0_PIN);
  gpio_init(D1_PIN);
  gpio_init(D2_PIN);
  gpio_init(D3_PIN);
  gpio_init(D4_PIN);
  gpio_init(D5_PIN);
  gpio_init(D6_PIN);
  gpio_init(D7_PIN);
}

/// @brief shiftAddress(uint32_t addr) shifts out the address specified
/// @param addr The address to set
void shiftAddress(uint32_t addr) {
  gpio_put(LATCH_PIN_NUMBER, false);
  gpio_put(DATA_PIN_NUMBER, false);
  gpio_put(CLOCK_PIN_NUMBER, false);

  bool state = false;
  for (int i = 0; i < ADDRESS_LINES; i++) {
    bool one = (addr & 0x01) != 0;
    addr >>= 1;
    if (one) {
      gpio_put(DATA_PIN_NUMBER, true);
    } else {
      gpio_put(DATA_PIN_NUMBER, false);
    }
    nop();
    gpio_put(CLOCK_PIN_NUMBER, true);
    nop();
    gpio_put(CLOCK_PIN_NUMBER, false);
    nop();
  }

  gpio_put(LATCH_PIN_NUMBER, true);
  nop();
  gpio_put(LATCH_PIN_NUMBER, false);
  nop();
}

/// @brief handleErr() is a function to blink the onboard LED and stop the pi if something went wrong.
void handleErr() {
  printf("Caught error, blinking onboard LED to indicate error.");

  gpio_put(ONBOARD_LED_PIN, true);
  sleep_ms(500);
  gpio_put(ONBOARD_LED_PIN, false);
  sleep_ms(500);
  gpio_put(ONBOARD_LED_PIN, true);
  sleep_ms(500);
  gpio_put(ONBOARD_LED_PIN, false);
  sleep_ms(500);
  gpio_put(ONBOARD_LED_PIN, true);
  sleep_ms(500);
  gpio_put(ONBOARD_LED_PIN, false);
  sleep_ms(500);
}

/// @brief oledDisplayMessages(..) takes 5 strings and prints them to the oled.
/// @param message1 - The top line of text to show
/// @param message2 - The next line to show... etc
void oledDisplayMessages(char *message1, char *message2, char *message3, char* message4, char* message5) {
  ssd1306_clear(&_display);
  ssd1306_draw_string(&_display, 0, 0, 1, message1);
  ssd1306_draw_string(&_display, 0, 10, 1, message2);
  ssd1306_draw_string(&_display, 0, 20, 1, message3);
  ssd1306_draw_string(&_display, 0, 30, 1, message4);
  ssd1306_draw_string(&_display, 0, 40, 1, message5);
  ssd1306_show(&_display);
}

/// @brief sets each data pin according to the input byte
/// @param byteOfData - the byte to set.
void setDataPins(uint8_t byteOfData) {
  bool state = false;
  const int pins[] = { D0_PIN, D1_PIN, D2_PIN, D3_PIN,
                      D4_PIN, D5_PIN, D6_PIN, D7_PIN };
  for (int i = 0; i < 8; i++) {  // This is always a byte, 8 bits.
    bool isOne = (byteOfData & 1) != 0;
    byteOfData >>= 1;
    gpio_put(pins[i], isOne);
  }
}

/// @brief // setReadMode() changes the data pins to inputs, and clears them if they were ON before that.
///           it also preps the EEPROM control pins to prepare to output the data (and input into our Pi).
void setReadMode() {
  const int pins[] = { D0_PIN, D1_PIN, D2_PIN, D3_PIN,
                      D4_PIN, D5_PIN, D6_PIN, D7_PIN };
  for (int i = 0; i < 8; i++) {
    if (gpio_get_dir(pins[i]) == 1) {
      gpio_put(pins[i], false);
      sleep_ms(1);
    }
    gpio_set_dir(pins[i], GPIO_IN);
    gpio_pull_down(pins[i]);
    sleep_ms(1);
  }

  gpio_put(WRITE_ENABLE_PIN, true);    // Set /WE to high (off)
  gpio_put(OUTPUT_ENABLE_PIN, false);  // Set /OE to low (on)
  gpio_put(CHIP_ENABLE_PIN, false);    // Set /CE to low (on)
  // At this point, the outputs are always on, changing the address controls the data output.
  sleep_ms(1);
}

/// @brief setWriteMode() sets the data pins to be outputs, and preps the EEPROM enable pins.
void setWriteMode() {
  const int pins[] = { D0_PIN, D1_PIN, D2_PIN, D3_PIN,
                      D4_PIN, D5_PIN, D6_PIN, D7_PIN };
  for (int i = 0; i < 8; i++) {
    gpio_set_dir(pins[i], GPIO_OUT);
    gpio_put(pins[i], false);
  }

  gpio_put(OUTPUT_ENABLE_PIN, true);  // Set /OE to high (off)
  gpio_put(CHIP_ENABLE_PIN, true);  // Set /CE to high (off)  - CE and WE must be kept high
  gpio_put(WRITE_ENABLE_PIN, true);  // Set /WE to high.
  sleep_ms(1);
}

/// @brief write(uint32_t address, uint8_t data) shifts out the address, then sets the
///        data pins to match the input byte. Finally, we toggle /CE and /WE to perform the write.
/// @param address - The destination address
/// @param data - The desired Byte to write
void write(uint32_t address, uint8_t data) {
  gpio_put(OUTPUT_ENABLE_PIN, true);
  gpio_put(WRITE_ENABLE_PIN, true);
  gpio_put(CHIP_ENABLE_PIN, false);
  nop();
  shiftAddress(address);
  setDataPins(data);
  nop();
  gpio_put(WRITE_ENABLE_PIN, false);
  sleep_us(1); // This should be 20 nano seconds, but even doing 500 nop commands does not work...
  gpio_put(WRITE_ENABLE_PIN, true);
  sleep_us(1);
  gpio_put(CHIP_ENABLE_PIN, true);
  sleep_us(25); // According to datasheet, this can take up to 20 microseconds.
}

/// @brief EEPROM_readByte(uint32_t address) reads the data at the supplied address from EEPROM.
///        First it shifts out hte address, then it reads each data pin and shifts that
///        bit into the return byte, until all 8 bits have been read.
/// @param address The address to read from. 
/// @return uint8_t data read from that address.
uint8_t EEPROM_readByte(uint32_t address) {
  uint8_t output = 0x0;
  const int pins[] = { D7_PIN, D6_PIN, D5_PIN, D4_PIN,
                      D3_PIN, D2_PIN, D1_PIN, D0_PIN };
  uint8_t currentBit = 0;
  bool currentBitState = false;
  shiftAddress(address);
  nop();
  for (int i = 0; i < 8; i++) {
    currentBitState = gpio_get(pins[i]);
    currentBit = currentBitState ? 1 : 0;
    output = output << 1 | currentBit;
  }

  return output;
}

/* SD Card function wrappers: */
/// @brief SD_init() - wrapper for sd_init_driver
bool SD_init() {
  bool result = sd_init_driver();
  sleep_ms(10);
  if (!result) {
    printf("SD Error! Could not init SD card.\n");
    oledDisplayMessages("SD Error!", "Could not", "init SD card.", "", "");
    handleErr();
    return false;
  }

  printf("SD Card init successful.\n");
  return true;
}

/// @brief SD_mount() - wrapper for f_mount
bool SD_mount(FATFS* fatfs) {
  FRESULT fr = f_mount(fatfs, "0:", 1);
  sleep_ms(10);
  if (fr != FR_OK) {
    printf("SD Error! Could not mount SD card.\n");
    oledDisplayMessages("SD Error!", "Could not", "mount SD card.", "", "");
    handleErr();
    return false;
  }

  printf("SD Card mount successful.\n");
  return true;
}

/// @brief SD_openFile() - wrapper for f_open
/// @param fp The file pointer to open
/// @param fileName The file name
/// @param readWrite The read / write mode to open the file
bool SD_openFile(FIL *fp, const TCHAR *fileName, BYTE readWrite) {
  FRESULT fr = f_open(fp, fileName, readWrite);
  sleep_ms(10);
  if (fr != FR_OK) {
    oledDisplayMessages("SD Error!", "Could not", "open file.", "", "");
    printf("SD Error! Could not open file!\n");
    handleErr();
    return false;
  }

  printf("SD Card openFile successful.\n");
  return true;
}

/// @brief SD_writeToFile() - wrapper for f_printf
/// @param fp The file pointer to write
/// @param message The data to write to the SD card
int SD_writeToFile(FIL *fp, const TCHAR *message) {
  return f_printf(fp, message);
}

/// @brief SD_closeFile() - wrapper for f_close
/// @param fp The file pointer to close
bool SD_closeFile(FIL *fp) {
  FRESULT fr = f_close(fp);
  if (fr != FR_OK) {
    printf("SD Error! Could not close file!\n");
    oledDisplayMessages("SD Error!", "Could not", "close file.", "", "");
    handleErr();
    return false;
  }

  printf("Successfully closed file on SD card.\n");
  return true;
}

/// @brief SD_unmount() - wrapper for f_unmount
void SD_unmount() {
  f_unmount("0:");
}

void handleByteMismatch(uint32_t address, uint8_t expectedData, uint8_t actualData) {
  char message1[32] = "Address: ";
  char message2[32] = "Expected: ";
  char message3[32] = "Actual: ";

  sprintf(message1, "%s 0x%05lX", message1, address);
  sprintf(message2, "%s 0x%02hX", message2, expectedData);
  sprintf(message3, "%s 0x%02hX", message3, actualData);
  oledDisplayMessages("Error! Byte mismatch", message1, message2, message3, "");

  char bigMessage[200] = "Error! Byte mismatch: ";
  sprintf(bigMessage, "%s %s %s %s", message1, message2, message3, "\n");
  printf(bigMessage);
  sleep_ms(2000);
}

/// @brief EEPROM_writeByte(..) writes data byte to address on the EEPROM.
/// @param address The destination address
/// @param data The data byte to be written
void EEPROM_writeByte(uint32_t address, uint8_t data) {
  write(0x5555, 0xAA);
  write(0x2AAA, 0x55);
  write(0x5555, 0xA0);
  write(address, data);
}

/// @brief EEPROM_chipErase() performs the 6-byte chip erase sequence.
void EEPROM_chipErase() {
  oledDisplayMessages("Erasing", "EEPROM", "now...", "", ""); // Erase happens so fast, you probably won't see this message.
  setWriteMode();
  write(0x5555, 0xAA); // 0x5555 0xAA
  write(0x2AAA, 0x55); // 0x2AAA 0x55
  write(0x5555, 0x80); // 0x5555 0x80
  write(0x5555, 0xAA); // 0x5555 0xAA
  write(0x2AAA, 0x55); // 0x2AAAH 0x55
  write(0x5555, 0x10); // 0x5555 0x10
  printf("Chip erase complete!\n");
  oledDisplayMessages("EEPROM", "erase", "complete!", "Waiting", "1 second.");
  sleep_ms(1000); // Datasheet says this takes up to 100ms.
}

void EEPROM_WriteCurrentFile(FIL* fil) {
  oledDisplayMessages("Writing File", "to EEPROM", "now...", "", "");
  setWriteMode();
  uint32_t address = 0;
  FRESULT result;
  const int BUFFER_SIZE = 1024; // Reads this many bytes from file at a time
  char buffer[BUFFER_SIZE]; // This is the buffer we will be reading data from disk into
  UINT numBytesRead = 0; // This is the number of bytes that the f_read function actually read.

  while (true) {
    result = f_read(fil, buffer, BUFFER_SIZE, &numBytesRead); // First, read N bytes from the file
    if (result != FR_OK) { break; }
    for (int i = 0; i < numBytesRead; i++) { // For each byte we read,
      EEPROM_writeByte(address, buffer[i]); // Write file to EEPROM
      address += 1;
    }

    if (numBytesRead < BUFFER_SIZE) { break; } // If we did not read a full buffer worth of info, we're done!
  }

  char stringTwo[32] = "Addrs: ";
  sprintf(stringTwo, "%s 0x%05lX", stringTwo, address);
  oledDisplayMessages("Done writing EEPROM!", "number of", stringTwo, "", "");
  sleep_ms(5000);
}

void EEPROM_ReadAndVerify(FIL* fil) {
  oledDisplayMessages("Reading file", "from EEPROM", "now...", "", "");
  setReadMode();
  uint32_t address = 0;
  uint8_t currentByte = 0;
  int errors = 0;
  FRESULT result;
  const int BUFFER_SIZE = 1024; // Reads this many bytes from file at a time
  char buffer[BUFFER_SIZE]; // This is the buffer we will be reading data from disk into
  UINT numBytesRead = 0; // This is the number of bytes that the f_read function actually read.

  while (true) {
    result = f_read(fil, buffer, BUFFER_SIZE, &numBytesRead); // First, read N bytes from the file
    if (result != FR_OK) { break; }
    for (int i = 0; i < numBytesRead; i++) { // For each byte we read,
      currentByte = EEPROM_readByte(address); // Write file to EEPROM
      // expect buffer[i] == currentByte
      if (currentByte != buffer[i]) {
        errors += 1;
        handleByteMismatch(address, buffer[i], currentByte);
      }

      address += 1;
    }

    if (numBytesRead < BUFFER_SIZE) { break; } // If we did not read a full buffer worth of info, we're done!
  }
  
  char stringTwo[32] = "Addrs: ";
  char stringThree[32] = "Num errors: ";
  sprintf(stringTwo, "%s 0x%05lX", stringTwo, address);
  sprintf(stringThree, "%s %d", stringThree, errors);
  oledDisplayMessages("Done reading EEPROM!", stringTwo, stringThree, "", "");
}

void EEPROM_VerifyErased() {
  oledDisplayMessages("Verifying", "EEPROM is", "erased now...", "", "");
  setReadMode();
  uint32_t address = 0;
  uint8_t currentByte = 0;
  int errors = 0;

  for (int i = 0; i < MAX_EEPROM_ADDRESS_SPACE; i++) { // For each byte on the chip,
    currentByte = EEPROM_readByte(address); // Read that byte
    if (currentByte != 0xFF) { // EEPROM erases all bytes to 0xFF
      errors += 1;
      handleByteMismatch(address, 0xFF, currentByte);
    }

    address += 1;
  }
  
  char stringTwo[32] = "Addrs: ";
  char stringThree[32] = "Num errors: ";
  sprintf(stringTwo, "%s 0x%05lX", stringTwo, address);
  sprintf(stringThree, "%s %d", stringThree, errors);
  oledDisplayMessages("Done reading EEPROM!", stringTwo, stringThree, "", "");
}

/// @brief sd_routine - Work in progress SD Card routine. Reads, erases, writes to EEPROM and SD stuff.
void sd_routine(char* fileName) {
  FATFS fat_fs;
  FIL fil1;

  printf("Beginning SD Card EEPROM routine!\n");
  char byteString[10]; // This is a string just used to display the Byte as hex like "0xA4"

  // Step 1: Set up the SD card and open the input file to read:
  SD_init();
  SD_mount(&fat_fs);
  SD_openFile(&fil1, fileName, FA_READ);
  
  oledDisplayMessages("Performing", "Chip Erase", "", "", "");
  EEPROM_chipErase();
  oledDisplayMessages("Chip Erase", "Done!", "", "", "");
  sleep_ms(1000);
  oledDisplayMessages("Verifying", "EEPROM", "is", "fully", "erased...");
  sleep_ms(200);
  EEPROM_VerifyErased();
  oledDisplayMessages("EEPROM", "is", "fully", "erased!", "");
  sleep_ms(1000);

  oledDisplayMessages("Writing data", "from SD card", "to EEPROM...", "", "");
  EEPROM_WriteCurrentFile(&fil1);
  oledDisplayMessages("Done", " writing EEPROM!", "", "", "");

  sleep_ms(1000);
  oledDisplayMessages("Verifying", "EEPROM now...", "", "", "");
  sleep_ms(1000);

  SD_closeFile(&fil1); // Close the first file
  FIL fil2;
  SD_openFile(&fil2, fileName, FA_READ); // Open again with a fresh reference
  EEPROM_ReadAndVerify(&fil2);
  sleep_ms(60000);

  // Now clean up after ourselves:
  sleep_ms(100);
  SD_closeFile(&fil2);
  sleep_ms(100);
  SD_unmount();
  sleep_ms(100);
}

/// @brief main - program entrypoint
/// @return exit code
int main() {
  setup(); // Setup IO pins first, always!
  sleep_ms(1000);
  FATFS fat_fs;
  SD_init();
  sleep_ms(1000);
  SD_mount(&fat_fs);
  sleep_ms(2000);

  char buf[3]; // TODO: Is it worth refactoring getChar to read a line? Like to get commands over serial?
  
  while (true) { 
    oledDisplayMessages("Use serial port", "r - read ROM", "w - write ROM", "e - erase ROM", "v - verify erased");
    buf[0] = getchar(); // Wait for user to press 'enter' to continue
    if (buf[0] == 'r') {
      FIL myFil;
      SD_openFile(&myFil, "marioduck.nes", FA_READ);
      EEPROM_ReadAndVerify(&myFil);
      SD_closeFile(&myFil);
      sleep_ms(3000);
    }

    if (buf[0] == 'w') {
      FIL writeFil;
      SD_openFile(&writeFil, "marioduck.nes", FA_READ);
      EEPROM_WriteCurrentFile(&writeFil);
      SD_closeFile(&writeFil);
      sleep_ms(3000);
    }

    if (buf[0] == 'e') {
      EEPROM_chipErase();
      sleep_ms(3000);
    }

    if (buf[0] == 'v') {
      EEPROM_VerifyErased();
      sleep_ms(3000);
    }

    if (buf[0] == 'q') {
      SD_unmount();
      return 0;
    }

    sleep_ms(1);
  }

  return 0;
}
