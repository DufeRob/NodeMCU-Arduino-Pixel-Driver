// include some libraries
#define USE_OCTOWS2811
#include <OctoWS2811.h>
#include <FastLED.h>

#include "EnviralDesign.h"

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

#define min(a,b) ((a)<(b)?(a):(b))
#define max(a,b) ((a)>(b)?(a):(b))

// Stream packet protocol
#define startMarker 254
#define endMarker 255
#define specialByte 253
#define SERIAL_TIMEOUT 200 // wait for next byte in stream when decoding a high byte

uint16_t serialBytesRecvd = 0;
uint16_t serialRecvCount = 0;
bool serialInProgress = false;
bool serialAllReceived = false;
byte * packetBuffer;

///////////////////// USER DEFINED VARIABLES START HERE /////////////////////////////
// NOTICE: these startup settings, especially pertaining to number of pixels and starting color
// will ensure that your nodeMCU can be powered on and run off of a usb 2.0 port of your computer.

String deviceName = "PxlNode-8266";

// number of physical pixels in the strip.
uint16_t pixelsPerStrip = 64;

// This needs to be evenly divisible by PIXLES_PER_STRIP.
// This represents how large our packets are that we send from our software source IN TERMS OF LEDS.
uint16_t chunkSize = 64;

// Dynamically limit brightness in terms of amperage.
float amps = 1;
uint16_t mAPerPixel = 60;

// Unused but kept for compatibility
uint16_t udpPort = 0;

//Set here the inital RGB color to show on module power up
byte InitColor[] = {200, 75, 10};

///////////////////// USER DEFINED VARIABLES END HERE /////////////////////////////

//Interfaces user defined variables with memory stored in EEPROM
EnviralDesign ed(&pixelsPerStrip, &chunkSize, &mAPerPixel, &deviceName, &amps, &udpPort, InitColor);

#define STREAMING_TIMEOUT 10  //  blank streaming frame after X seconds

// If this is set to 1, a lot of debug data will print to the console.
// Will cause horrible stuttering meant for single frame by frame tests and such.
#define DEBUG_MODE 0 //MDB
#define PACKETDROP_DEBUG_MODE 0

#define NUM_STRIPS 8
CRGB *leds;

//Set here the inital RGB color to show on module power up
CRGB LastColor=CRGB(0,0,0);  //hold the last colour in order to stitch one effect with the following.

// used later for holding values - used to dynamically limit brightness by amperage.
uint32_t milliAmpsLimit = amps * 1000;

// Reply buffer, for now hardcoded but this might encompass useful data like dropped packets etc.
byte ReplyBuffer[1 + MAX_NAME_LENGTH + VARIABLES_LENGTH] = {0};
byte counterHolder = 0;

unsigned long lastStreamingFrame=0;

void setup() {
  
  ////////////////// A whole bunch of initialization stuff that prints no matter what.
  Serial.begin(115200);
  if (DEBUG_MODE) {
    Serial.println();
    Serial.println();
    Serial.println(F("Serial started")); 
  }
  ed.setCompile(String(__TIME__));    //Compiling erases variables previously changed over the network
  ed.start(); 
  
  //Initializes FastLED
  startFastLED();
  
  //Sets the size of the Serial packets
  setPacketSize();

  //Animate from dark to initial color in 3 seconds on module power up
  initDisplay();

  // Set milliamps value
  milliAmpsLimit = amps * 1000;

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

}

void loop() { //main program loop  
  getSerialData();
  opcode = parseSerialPoll();
  
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
  } else if (lastStreamingFrame!=0 && millis()-lastStreamingFrame>STREAMING_TIMEOUT*1000) {
      if (DEBUG_MODE) {
        Serial.println(F("Streaming timeout"));
      }
      blankFrame();
      lastStreamingFrame=0;
  }
}

void getSerialData() {
  if (Serial.available() > 0) {

    byte x = Serial.read();
    
    if (x == startMarker) {
    
      serialBytesRecvd = 0;
      serialInProgress = true;
    
    } else if (x == endMarker) {
      
      serialInProgress = false;
      serialAllReceived = true;
    
    } else if (serialInProgress) {
    
      if (x == specialByte) {
        unsigned long startTime = millis();
        while(millis() - startTime < SERIAL_TIMEOUT) { // To decode the highbyte
          if (Serial.available() > 0) {
            x += Serial.read();
            break;
          }
        }
      }
      packetBuffer[serialBytesRecvd++] = x;
    }
  }
}

void blankFrame() {
  paintFrame(CRGB(0,0,0));
  LEDS.show();
}

void paintFrame(CRGB c) {
  c=adjustToMaxMilliAmps(c);
  for (uint32_t i = 0; i < pixelsPerStrip * NUM_STRIPS; i++) {
    leds[i] = c;
  }
}

CRGB adjustToMaxMilliAmps(CRGB c) {
  float ma = (mAPerPixel/3) * ( c[0] + c[1] + c[2] ) / 255.0 * pixelsPerStrip * NUM_STRIPS;
  CRGB r = c;

  if (ma > milliAmpsLimit) {
    r *= milliAmpsLimit/ma;
  }

  return r;

}


void playStreaming(int chunkID) {
  
  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    Serial.println(F("---Incoming---"));
    Serial.print(F("ChunkID: "));
    Serial.println(chunkID);
  }

  // Figure out what our starting offset is.
  //const uint16_t initialOffset = chunkSize * (action - 1);
  const uint16_t initialOffset = chunkSize * chunkID;
  
  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    Serial.print(F("---------: "));
    Serial.print(chunkSize);
    Serial.print(F("   "));
    Serial.println(F(""));
    Serial.print(F("Init_offset: "));
    Serial.println(initialOffset);
    Serial.print(F(" ifLessThan: "));
    Serial.println((initialOffset + chunkSize));
  }

  // loop through our recently received packet, and assign the corresponding
  // RGB values to their respective places in the strip.
  uint16_t led=0;
  byte r;
  byte g;
  byte b;

  uint32_t numOfPixels = pixelsPerStrip * NUM_STRIPS;
  
  for (uint32_t i = 1; i < chunkSize*3;) {

    uint32_t index = initialOffset+led++;

    if (index >= numOfPixels) {
      break;
    }
    
    r = packetBuffer[i++];
    g = packetBuffer[i++];
    b = packetBuffer[i++];
    
    leds[index] = CRGB(r, g, b);
  }

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    Serial.println(F("Finished For Loop!"));
  }

  // if we're debugging packet drops, modify reply buffer.
  if (PACKETDROP_DEBUG_MODE) {
    ReplyBuffer[chunkID] = 1;
  }

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    Serial.println(F("--end of packet and stuff--"));
    Serial.println(F(""));
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
    Serial.println(F("Starting Fast LED"));
  }
  if (leds) {
    delete leds;
  }
 
  leds = (CRGB *)malloc(sizeof(CRGB) * NUM_STRIPS * pixelsPerStrip);

  LEDS.addLeds<OCTOWS2811, RGB>(leds, pixelsPerStrip);
  LEDS.clearData();
  LEDS.setBrightness(255);
}

void setPacketSize() {
  if (packetBuffer) free(packetBuffer);
  // Max packet size is the OPCODE + ( RGB[chunksize][3] OR Update size )
  // Update size MAX_NAME_LENGTH + sizeof(PixelsPerStrip, ChunkSize, UdpPort, AmpsLimit, MaPerPixel, WarmUpColor)
  uint16_t packetSize = ( 1 + max( (chunkSize*3), (MAX_NAME_LENGTH  + VARIABLES_LENGTH) ) );
  packetBuffer = (byte *)malloc(packetSize);//buffer to hold incoming packets
}

void initDisplay() {  
  CRGB InitialColor=adjustToMaxMilliAmps(CRGB(InitColor[0],InitColor[1],InitColor[2]));
  for(int i=0;i<=90;i++) {
    paintFrame(CRGB(InitialColor[0]*i/90.0,InitialColor[1]*i/90.0,InitialColor[2]*i/90.0));
    LEDS.show();
    delay(16);
  };
}

// Returns the opcode
int parseSerialPoll() {

  if (!serialAllReceived) {
    return NOPACKET;
  } else {
    serialAllReceived = false;
  }
  
  return packetBuffer[0];
}

void serialUpdateFrame() {

  if (PACKETDROP_DEBUG_MODE) {
    Serial.println("Updating Frame");
  }
  digitalWrite(LED_BUILTIN, HIGH);

  // this math gets our sum total of r/g/b vals down to milliamps (~60mA per pixel)
  uint32_t milliAmpsCounter = 0;
  CRGB *pixelBuf = LEDS.leds();
  uint32_t pixelSize = LEDS.size();
  double conversion = (mAPerPixel / 3.0) / 255.0;
  for (uint32_t i = 0; i < pixelSize; i++) {
    milliAmpsCounter += pixelBuf[i][0];//R
    milliAmpsCounter += pixelBuf[i][1];//G
    milliAmpsCounter += pixelBuf[i][2];//B
  }
  
  if (PACKETDROP_DEBUG_MODE) {
    Serial.print(F("Raw rgb values for mA limiter: "));Serial.println(milliAmpsCounter);
    Serial.print(F("Conversion: "));Serial.println(conversion);
    Serial.print(F("PixelSize: "));Serial.println(pixelSize);
  }

  milliAmpsCounter = floor((double)milliAmpsCounter * conversion);

  if (PACKETDROP_DEBUG_MODE) {
    Serial.print(F("Converted mA value: "));Serial.println(milliAmpsCounter);
  }

  byte millisMultiplier = (byte)( constrain( ((float)milliAmpsLimit / (float)milliAmpsCounter), 0, 1) * 255);

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    Serial.println(F("Trying to update leds..."));
    Serial.print(F("Dimming leds to: "));
    Serial.println( millisMultiplier );
  }

  // We already applied our r/g/b values to the strip, but we haven't updated it yet.
  // Since we needed the sum total of r/g/b values to calculate brightness, we
  // can loop through all the values again now that we have the right numbers
  // and scale brightness if we need to.  
  if(millisMultiplier!=255) { //dim LEDs only if required
    LEDS.setBrightness(millisMultiplier); //
  }
  LEDS.show();   // write all the pixels out
  LEDS.setBrightness(255); // Reset brightness limiter
  lastStreamingFrame=millis();
  //milliAmpsCounter = 0; // reset the milliAmpsCounter for the next frame.

  if (PACKETDROP_DEBUG_MODE) { // If Debug mode is on print some stuff
    Serial.println(F("Finished updating Leds!"));
  }

  // if we're debugging packet drops, modify reply buffer.
  if (PACKETDROP_DEBUG_MODE) {
    // set the last byte of the reply buffer to 2, indiciating that the frame was sent to leds.
    ReplyBuffer[sizeof(ReplyBuffer) - 1] = 2;
    ReplyBuffer[0] = counterHolder;
    counterHolder += 1;

    // clear the response buffer string.
    for (byte i = 0; i < sizeof(ReplyBuffer); i++) {
      Serial.write(ReplyBuffer[i]);
      ReplyBuffer[i] = 0;
    }
    Serial.println();
  }

  digitalWrite(LED_BUILTIN, LOW);
}

void serialConfigDevice() {
  // Set packetbuffer index past the OpCode byte
  int i = 1;
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
    Serial.println(F("Replying..."));
  }

  Serial.write(ReplyBuffer, sizeof(ReplyBuffer));

  if (PACKETDROP_DEBUG_MODE) {
    Serial.println(F("EndReplyBuffer"));
  }
}
