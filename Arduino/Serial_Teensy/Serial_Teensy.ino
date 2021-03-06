// include some libraries
#define USE_OCTOWS2811
#include <OctoWS2811.h>
#include <FastLED.h>

#include "EnviralDesign.h"

//Change which Serial port the device listens for commands and outputs debugging info.
#define DEBUG_PORT Serial
#define INPUT_PORT Serial

// Streaming Poll Opcodes
#define CHUNKIDMIN 0
#define CHUNKIDMAX 99
#define UPDATEFRAME 100
#define POLL 200
#define POLLREPLY 201
#define CONFIG 202
#define NOPACKET -1
#define VARIABLES_LENGTH 15 // sum of bytes for user variables. 2 (pixelsPerStrip) + 2 (chunkSize) + 2 (udpPort) + 4 (ampLimit) + 2 (maPerPixel) + 3 (WarmUpColor)
int opcode;

// Stream packet protocol
#define SERIAL_TIMEOUT 200 // Max time to wait for a serial packet in milliseconds

// Holds data from serial
uint8_t * packetBuffer;

uint16_t frameCounter = 0;
unsigned long timeA;
unsigned long timeB;
unsigned long timeC;
unsigned long timeD;
unsigned long timeE;

unsigned long timeZ;

///////////////////// USER DEFINED VARIABLES START HERE /////////////////////////////
// NOTICE: these startup settings, especially pertaining to number of pixels and starting color
// will ensure that your nodeMCU can be powered on and run off of a usb 2.0 port of your computer.

String deviceName = "PxlNode-Serial";

// number of physical pixels in the strip.
uint16_t pixelsPerStrip = 400; // for teensy, 400 * 8 = 3,200

// This needs to be evenly divisible by PIXLES_PER_STRIP.
// This represents how large our packets are that we send from our software source IN TERMS OF LEDS.
uint16_t chunkSize = 3200;

// Dynamically limit brightness in terms of amperage.
float amps = 40;
uint16_t mAPerPixel = 60;

// Unused but kept for compatibility
uint16_t udpPort = 0;

//Set here the inital RGB color to show on module power up
byte InitColor[] = {200, 75, 10};

///////////////////// USER DEFINED VARIABLES END HERE /////////////////////////////

//Interfaces user defined variables with memory stored in EEPROM
EnviralDesign ed(&pixelsPerStrip, &chunkSize, &mAPerPixel, &deviceName, &amps, &udpPort, InitColor);

#define STREAMING_TIMEOUT 10000  //  blank streaming frame after X milliseconds

// If this is set to 1, a lot of debug data will print to the console.
// Will cause horrible stuttering meant for single frame by frame tests and such.
#define DEBUG_MODE 0 //MDB
#define PACKETDROP_DEBUG_MODE 0
#define OPTIMIZE_DEBUG_MODE 0

#define NUM_STRIPS 8
CRGB *leds;

/** Pin layouts on the teensy 3:

  --------------------
    pin 2:  LED Strip #1    OctoWS2811 drives 8 LED Strips.
    pin 14: LED strip #2    All 8 are the same length.
    pin 7:  LED strip #3
    pin 8:  LED strip #4    A 100 ohm resistor should used
    pin 6:  LED strip #5    between each Teensy pin and the
    pin 20: LED strip #6    wire to the LED strip, to minimize
    pin 21: LED strip #7    high frequency ringining & noise.
    pin 5:  LED strip #8
**/
//Set here the inital RGB color to show on module power up
//CRGB LastColor=CRGB(0,0,0);  //hold the last colour in order to stitch one effect with the following.

//used to dynamically limit brightness by amperage.
uint32_t milliAmpsLimit;

// Reply buffer, for now hardcoded but this might encompass useful data like dropped packets etc.
byte ReplyBuffer[1 + MAX_NAME_LENGTH + VARIABLES_LENGTH] = {0};
byte counterHolder = 0;

unsigned long lastStreamingFrame=0;

void setup() {
  
  ////////////////// A whole bunch of initialization stuff that prints no matter what.
  if (DEBUG_MODE || PACKETDROP_DEBUG_MODE || OPTIMIZE_DEBUG_MODE) {
    DEBUG_PORT.begin(115200);
  }
  INPUT_PORT.begin(3000000);
  INPUT_PORT.setTimeout(SERIAL_TIMEOUT);
  
  if (DEBUG_MODE) {
    DEBUG_PORT.println();
    DEBUG_PORT.println();
    DEBUG_PORT.println(F("Serial started"));
    DEBUG_PORT.flush();
    delay(100);
  }
  ed.setCompile(String(__TIME__));    //Compiling erases variables previously changed over the network
  ed.start(); 
  // Set milliamps value
  milliAmpsLimit = amps * 1000;
  
  //Initializes FastLED
  startFastLED();
  
  //Sets the size of the Serial packets
  setPacketSize();

  //Animate from dark to initial color in 3 seconds on module power up
  initDisplay();

}

void loop() { //main program loop  
  if (OPTIMIZE_DEBUG_MODE) {
    timeA = micros();
  }
  //DEBUG_PORT.println(frameCounter);
  opcode = getSerialData();
  
  // opcodes between 0 and 99 represent the chunkID
  if (opcode <= CHUNKIDMAX && opcode >= CHUNKIDMIN) {
    playStreaming(opcode);
    
  } else if (opcode == UPDATEFRAME) {
    serialUpdateFrame();
    
  } else if (opcode == CONFIG) {
    serialConfigDevice();
    
  } else if (opcode == POLL) {
    serialSendPollReply();
    
  } else if (opcode == POLLREPLY) {
    //POLLREPLY safe to ignore    
  // Streaming but nothing received check timeout
  } else if ( lastStreamingFrame != 0 && millis() - lastStreamingFrame > STREAMING_TIMEOUT ) {
      if (DEBUG_MODE) {
        DEBUG_PORT.println(F("Streaming timeout"));
      }
      blankFrame();
      blankPacket();
      lastStreamingFrame=0;
  }  
  
  //DEBUG_PORT.println(F(frameCounter + "Frame End"));
  //DEBUG_PORT.println();
  if (OPTIMIZE_DEBUG_MODE) {
    timeZ = micros();
  }
   if (OPTIMIZE_DEBUG_MODE) {
     if (opcode != -1) {
       //DEBUG_PORT.println(  );
       DEBUG_PORT.print( " OPCODE:" );
       DEBUG_PORT.print( opcode );
       DEBUG_PORT.print( " , " );
       DEBUG_PORT.print( " TOTAL ELAPSED:" );
       DEBUG_PORT.print( float(timeZ - timeA) / 1000 );
       DEBUG_PORT.println( "ms , " );
     }
   }
  
  frameCounter += 1;
}

// Build packet and return the opcode
int getSerialData() {  

  if ( INPUT_PORT.available() > 0) {
    unsigned long packetBuildStart;
    
    if (OPTIMIZE_DEBUG_MODE) {
      packetBuildStart = millis();
    }

    int opcode_found = INPUT_PORT.read();

    if (opcode_found <= CHUNKIDMAX && opcode_found >= CHUNKIDMIN) {

      size_t num_read = INPUT_PORT.readBytes(packetBuffer, chunkSize * 3);
      if (DEBUG_MODE) {
        DEBUG_PORT.print(F("Bytes read "));DEBUG_PORT.println(num_read);
      }
      
    } else if (opcode_found == UPDATEFRAME) {

      //Do nothing
    
    } else if (opcode_found == CONFIG) {

      size_t num_read = INPUT_PORT.readBytes(packetBuffer, MAX_NAME_LENGTH + VARIABLES_LENGTH);
      if (DEBUG_MODE) {
        DEBUG_PORT.print(F("Bytes read "));DEBUG_PORT.println(num_read);
      }
    
    } else if (opcode_found == POLL) {

      // Do nothing
    
    } else if (opcode_found == POLLREPLY) {

      // Nothing to do with the reply so dump it
      while ( INPUT_PORT.available() > 0 ) {
        INPUT_PORT.read();
      }
    
    // Unrecognized opcode
    } else {
      
      if (DEBUG_MODE) {
        DEBUG_PORT.println(F("Unrecognized opcode"));
      }

      while ( INPUT_PORT.available() > 0 ) {
        INPUT_PORT.read();
      }
      
      opcode_found = NOPACKET;
    }
    if (OPTIMIZE_DEBUG_MODE) {
      unsigned long timeTaken = millis() - packetBuildStart;
      DEBUG_PORT.print(F("Packet build time (ms): "));DEBUG_PORT.println(timeTaken);
    }

    return opcode_found;

  } else {
    return NOPACKET;
  }
}

uint16_t getPacketSize() {
  return ( max( (chunkSize*3), (MAX_NAME_LENGTH  + VARIABLES_LENGTH) ) );
}

// Max packet size is the OPCODE + ( RGB[chunksize][3] OR Update size )
// Update size MAX_NAME_LENGTH + sizeof(PixelsPerStrip, ChunkSize, UdpPort, AmpsLimit, MaPerPixel, WarmUpColor)
void blankPacket() {
  uint16_t packetSize = getPacketSize();
  for (uint16_t i = 0; i < packetSize; i++) {
    packetBuffer[i] = 0;
  }
}

void blankFrame() {
  paintFrame(CRGB(0,0,0));
  FastLED.show();
}

void paintFrame(CRGB c) {
  c=adjustToMaxMilliAmps(c);
  for (uint32_t i = 0; i < pixelsPerStrip * NUM_STRIPS; i++) {
    leds[i] = c;
  }
}

CRGB adjustToMaxMilliAmps(CRGB c) {
  //String inputColor = "Input " + String(c[0]) + ", " + String(c[1]) + ", " + String(c[2]);
  //DEBUG_PORT.println(inputColor);
  float ma = (mAPerPixel/3) * ( c[0] + c[1] + c[2] ) / 255.0 * pixelsPerStrip * NUM_STRIPS;
  //DEBUG_PORT.print("Calc'd ma: ");DEBUG_PORT.println(ma);
  CRGB r = CRGB(c);

  if (ma > milliAmpsLimit) {
    float factor = milliAmpsLimit/ma;
    //DEBUG_PORT.print("Limiting amps by ");DEBUG_PORT.println(factor);
    r[0] = r[0] * factor;
    r[1] = r[1] * factor;
    r[2] = r[2] * factor;
    //String result = "Result " + String(r[0]) + ", " + String(r[1]) + ", " + String(r[2]);
    //DEBUG_PORT.println(result);
  }

  return r;

}

void playStreaming(int chunkID) {
  
  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    DEBUG_PORT.println(F("---Incoming---"));
    DEBUG_PORT.print(F("ChunkID: "));
    DEBUG_PORT.println(chunkID);
  }

  // Figure out what our starting offset is.
  //const uint16_t initialOffset = chunkSize * (action - 1);
  const uint16_t initialOffset = chunkSize * chunkID;
  
  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    DEBUG_PORT.print(F("---------: "));
    DEBUG_PORT.print(chunkSize);
    DEBUG_PORT.print(F("   "));
    DEBUG_PORT.println(F(""));
    DEBUG_PORT.print(F("Init_offset: "));
    DEBUG_PORT.println(initialOffset);
    DEBUG_PORT.print(F(" ifLessThan: "));
    DEBUG_PORT.println((initialOffset + chunkSize));
  }

  // loop through our recently received packet, and assign the corresponding
  // RGB values to their respective places in the strip.
  uint16_t index=initialOffset;
  uint8_t r;
  uint8_t g;
  uint8_t b;

  uint32_t numOfPixels = pixelsPerStrip * NUM_STRIPS;
  
  for (uint32_t i = 0; i < chunkSize*3;) {

    r = packetBuffer[i++];
    g = packetBuffer[i++];
    b = packetBuffer[i++];

    leds[index++] = CRGB(r, g, b);
    if (index >= numOfPixels) {
      break;
    }
  }

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    DEBUG_PORT.println(F("Finished For Loop!"));
  }

  // if we're debugging packet drops, modify reply buffer.
  if (PACKETDROP_DEBUG_MODE) {
    ReplyBuffer[chunkID] = 1;
  }

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    DEBUG_PORT.println(F("--end of packet and stuff--"));
    DEBUG_PORT.println(F(""));
  }
}

bool updatePixels(int val) {
  if (val < 0 || val > 1500) {
    return false;
  } else {
    ed.updatePixelsPerStrip(val);

    return true;
  }
}

bool updateChunk(int val) {
  ed.updateChunkSize(val);
  return true;
}

bool updateMA(int val) {
  ed.updatemaPerPixel(val);

  return true;
}

bool updateName(String val) {
  ed.updateDeviceName(val);
  return true;
}

bool updateAmps(float val) {
  ed.updateAmps(val);
  
  // Update milliamps value
  milliAmpsLimit = amps * 1000;
  return true;
}

bool updateUDP(int val) {
  ed.updateUDPport(val);
  return true;
}

bool updateWarmUp(byte v1, byte v2, byte v3) {
  byte parameters[3] = {v1, v2, v3};
  ed.updateInitColor(parameters);
  return true;
}

void startFastLED() {
  if (DEBUG_MODE) {
    DEBUG_PORT.println(F("Starting Fast LED"));
  }
  
  if (leds) {
    delete leds;
  }

  leds = (CRGB*)malloc(sizeof(CRGB) * pixelsPerStrip * NUM_STRIPS);

  if (DEBUG_MODE) {
    DEBUG_PORT.println(F("LED buffer created"));
  }
  
  FastLED.addLeds<OCTOWS2811>(leds, pixelsPerStrip);

  FastLED.setBrightness(255);
  if (DEBUG_MODE) {
    DEBUG_PORT.println(F("Finished Fast LED setup"));
  }
}

void setPacketSize() {
  if (DEBUG_MODE){
    DEBUG_PORT.println(F("Setting packet size"));
  }
  if (packetBuffer) {
    delete packetBuffer;
  }
  uint16_t packetSize = getPacketSize();
  packetBuffer = (uint8_t *)malloc(packetSize);//buffer to hold incoming packets
  if (DEBUG_MODE) {
    DEBUG_PORT.print(F("Packet size set to: "));DEBUG_PORT.println(packetSize);
  }
}

void initDisplay() {
  if (DEBUG_MODE) {
    DEBUG_PORT.println(F("Initializing display"));
  }
  CRGB InitialColor=adjustToMaxMilliAmps(CRGB(InitColor[0],InitColor[1],InitColor[2]));
  for(int i=0;i<=90;i++) {
    paintFrame(CRGB(InitialColor[0]*i/90.0,InitialColor[1]*i/90.0,InitialColor[2]*i/90.0));
    FastLED.show();
    delay(16);
  };
  if (DEBUG_MODE) {
    DEBUG_PORT.println(F("Display Initialized"));
  }
}

void serialUpdateFrame() {

  if (PACKETDROP_DEBUG_MODE) {
    DEBUG_PORT.println("Updating Frame");
  }

  // this math gets our sum total of r/g/b vals down to milliamps (~60mA per pixel)
  uint32_t milliAmpsCounter = 0;
  CRGB *pixelBuf = FastLED.leds();
  uint32_t pixelSize = FastLED.size();
  double conversion = (mAPerPixel / 3.0) / 255.0;
  for (uint32_t i = 0; i < pixelSize; i++) {
    milliAmpsCounter += pixelBuf[i][0];//R
    milliAmpsCounter += pixelBuf[i][1];//G
    milliAmpsCounter += pixelBuf[i][2];//B
  }
  
  if (PACKETDROP_DEBUG_MODE) {
    DEBUG_PORT.print(F("Raw rgb values for mA limiter: "));DEBUG_PORT.println(milliAmpsCounter);
    DEBUG_PORT.print(F("Conversion: "));DEBUG_PORT.println(conversion);
    DEBUG_PORT.print(F("PixelSize: "));DEBUG_PORT.println(pixelSize);
  }

  milliAmpsCounter = floor((double)milliAmpsCounter * conversion);

  if (PACKETDROP_DEBUG_MODE) {
    DEBUG_PORT.print(F("Converted mA value: "));DEBUG_PORT.println(milliAmpsCounter);
  }

  byte millisMultiplier = (byte)( constrain( ((float)milliAmpsLimit / (float)milliAmpsCounter), 0, 1) * 255);

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    DEBUG_PORT.println(F("Trying to update leds..."));
    DEBUG_PORT.print(F("Dimming leds to: "));
    DEBUG_PORT.println( millisMultiplier );
  }

  // We already applied our r/g/b values to the strip, but we haven't updated it yet.
  // Since we needed the sum total of r/g/b values to calculate brightness, we
  // can loop through all the values again now that we have the right numbers
  // and scale brightness if we need to.  
  if(millisMultiplier!=255) { //dim LEDs only if required
    FastLED.setBrightness(millisMultiplier); //
  }
  FastLED.show();   // write all the pixels out
  FastLED.setBrightness(255); // Reset brightness limiter
  lastStreamingFrame=millis();

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    DEBUG_PORT.println(F("Finished updating Leds!"));
  }

  // if we're debugging packet drops, modify reply buffer.
  if (PACKETDROP_DEBUG_MODE) {
    // set the last byte of the reply buffer to 2, indiciating that the frame was sent to leds.
    ReplyBuffer[sizeof(ReplyBuffer) - 1] = 2;
    ReplyBuffer[0] = counterHolder;
    counterHolder += 1;

    // clear the response buffer string.
    for (byte i = 0; i < sizeof(ReplyBuffer); i++) {
      INPUT_PORT.write(ReplyBuffer[i]);
      ReplyBuffer[i] = 0;
    }
  }

//  digitalWrite(LED_BUILTIN, LOW);
}

void serialConfigDevice() {
  int i = 0;
  // Get the device name and save it to a buffer
  char nameBuf[MAX_NAME_LENGTH];
  for (int j = 0; j < MAX_NAME_LENGTH; j++) {
    nameBuf[j] = packetBuffer[i++];
  }
  nameBuf[MAX_NAME_LENGTH - 1] = '\0';
  updateName(String(nameBuf));
  byte valBuf[3];
  
  valBuf[0] = packetBuffer[i++];
  valBuf[1] = packetBuffer[i++];
  updatePixels(valBuf[0] * 256 + valBuf[1]);
  
  valBuf[0] = packetBuffer[i++];
  valBuf[1] = packetBuffer[i++];
  updateChunk(valBuf[0] * 256 + valBuf[1]);
  
  valBuf[0] = packetBuffer[i++];
  valBuf[1] = packetBuffer[i++];
  updateUDP(valBuf[0] * 256 + valBuf[1]);
  
  FLOAT_ARRAY tempF;
  for (int j = 0; j < 4; j++) {
    tempF.bytes[j] = packetBuffer[i++];
  }
  updateAmps(tempF.num);
  
  valBuf[0] = packetBuffer[i++];
  valBuf[1] = packetBuffer[i++];
  updateMA(valBuf[0] * 256 + valBuf[1]);
  
  valBuf[0] = packetBuffer[i++];
  valBuf[1] = packetBuffer[i++];
  valBuf[2] = packetBuffer[i++];
  updateWarmUp(valBuf[0], valBuf[1], valBuf[2]);

  //Initializes FastLED
  startFastLED();
  
  //Sets the size of the Serial packets
  setPacketSize();

  //Animate from dark to initial color in 3 seconds on module power up
  initDisplay();

  // Set milliamps value
  milliAmpsLimit = amps * 1000;
  
}
 
void serialSendPollReply() {
  int i = 0;

  // Set opcode to POLLREPLY
  ReplyBuffer[i++] = POLLREPLY;

  //Copy device name to reply buffer
  for (int j = 0; j < MAX_NAME_LENGTH; j++) {
    if (j < deviceName.length()) {
      ReplyBuffer[i++] = deviceName[j];
    } else {
      ReplyBuffer[i++] = '\0';
    }
  }
  
  //Copy pixelsPerStrip value
  ReplyBuffer[i++] = highByte(pixelsPerStrip);
  ReplyBuffer[i++] = lowByte(pixelsPerStrip);

  //Copy chunkSize value
  ReplyBuffer[i++] = highByte(chunkSize);
  ReplyBuffer[i++] = lowByte(chunkSize);

  //Copy udpPort value
  ReplyBuffer[i++] = highByte(udpPort);
  ReplyBuffer[i++] = lowByte(udpPort);

  //Copy amp limit value
  FLOAT_ARRAY tempf;
  tempf.num = amps;
  for (int j = 0; j < 4; j++) {
    ReplyBuffer[i++] = tempf.bytes[j];
  }

  //Copy the ma per pixel
  ReplyBuffer[i++] = highByte(mAPerPixel);
  ReplyBuffer[i++] = lowByte(mAPerPixel);

  //Copy the warmup color
  ReplyBuffer[i++] = InitColor[0];
  ReplyBuffer[i++] = InitColor[1];
  ReplyBuffer[i++] = InitColor[2];
  if (PACKETDROP_DEBUG_MODE) {
    DEBUG_PORT.println(F("Replying..."));
  }

  INPUT_PORT.write(ReplyBuffer, sizeof(ReplyBuffer));

  if (PACKETDROP_DEBUG_MODE) {
    DEBUG_PORT.println(F("EndReplyBuffer"));
  }
}
