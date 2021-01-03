//   Project: 14 Band Spectrum Analyzer using WS2812B/SK6812
//   Target Platform: Arduino Mega2560 or Mega2560 PRO MINI  
//   The original code has been modified by PLATINUM to allow a scalable platform with many more bands.
//   It is not possible to run modified code on a UNO,NANO or PRO MINI. due to memory limitations.
//   The library Si5351mcu is being utilized for programming masterclock IC frequencies. 
//   Special thanks to Pavel Milanes for his outstanding work. https://github.com/pavelmc/Si5351mcu
//   Analog reading of MSGEQ7 IC1 and IC2 use pin A0 and A1.
//   Clock pin from MSGEQ7 IC1 and IC2 to Si5351mcu board clock 0 and 1
//   Si5351mcu SCL and SDA use pin 20 and 21
//   See the Schematic Diagram for more info
//   Programmed and tested by PLATINUM
//   Version 1.0    
//***************************************************************************************************

#include <Adafruit_NeoPixel.h>
#include <si5351mcu.h>    //Si5351mcu library
Si5351mcu Si;             //Si5351mcu Board
#define PULSE_PIN         13 
#define NOISE             50
#define ROWS              20  //num of row MAX=20
#define COLUMNS           14  //num of column
#define DATA_PIN          9   //led data pin
#define STROBE_PIN        6   //MSGEQ7 strobe pin
#define RESET_PIN         7   //MSGEQ7 reset pin
#define CHANGE_MODE_PIN   2   //Change mode pin
#define RED_MODE_PIN      47  //Red mode pin
#define GREEN_MODE_PIN    46  //Green mode pin


#define NUMPIXELS    ROWS * COLUMNS

struct Point {
  uint32_t hsvColor;
  char  r,g,b;
  bool active;
};

struct PointColor {
  char r,g,b;
  uint32_t hsvColor;  
};


struct TopPoint {
  int pointPosition;
  int peakpause;
};

PointColor colors[ROWS][COLUMNS];

uint32_t hsvColors[ROWS][COLUMNS];
uint32_t hsvTopColors[COLUMNS];


PointColor topColor;

Point spectrum[ROWS][COLUMNS];

TopPoint peakhold[COLUMNS];

int spectrumValue[COLUMNS];
long int counter = 0;
int long pwmpulse = 0;
bool toggle = false;
int long time_change = 0;
int effect = 0;
int peakPause = 15;
int sinkDelay = 0;
int long lastSync = micros();
int brightness = 20;
int lastBrightness = 20;


volatile int long last_pressed = millis();
volatile bool autoMode = true;
int changeModeTime = 1000 * 10;
int long lastAutoChange = millis();
volatile int currentMode = 1;
volatile int lastMode = 1;

int totalModes = 6;


Adafruit_NeoPixel pixels = Adafruit_NeoPixel(NUMPIXELS, DATA_PIN, NEO_GRB + NEO_KHZ800);

void setup()  {
  Si.init(25000000L);
  Si.setFreq(0, 104570);
  Si.setFreq(1, 166280);
  Si.setPower(0, SIOUT_8mA);
  Si.setPower(1, SIOUT_8mA);
  Si.enable(0);
  Si.enable(1);
 
  pinMode      (STROBE_PIN,       OUTPUT);
  pinMode      (RESET_PIN,        OUTPUT);
  pinMode      (DATA_PIN,         OUTPUT);
  pinMode      (PULSE_PIN,        OUTPUT); 
  pinMode      (RED_MODE_PIN,     OUTPUT); 
  pinMode      (GREEN_MODE_PIN,   OUTPUT); 
  pinMode      (CHANGE_MODE_PIN,  INPUT_PULLUP); 

  attachInterrupt(digitalPinToInterrupt(CHANGE_MODE_PIN), onModChange, FALLING);
  
  digitalWrite(PULSE_PIN, HIGH);
  delay(100);
  digitalWrite(PULSE_PIN, LOW);
  delay(100);
  digitalWrite(PULSE_PIN, HIGH);
  delay(100);
  digitalWrite(PULSE_PIN, LOW);
  delay(100);
  digitalWrite(PULSE_PIN, HIGH);
  delay(100);
  
  pixels.setBrightness(brightness); //set Brightness
  pixels.begin();
  pixels.show();
  
  digitalWrite (RESET_PIN,  LOW);
  digitalWrite (STROBE_PIN, LOW);
  delay        (1);  
  digitalWrite (RESET_PIN,  HIGH);
  delay        (1);
  digitalWrite (RESET_PIN,  LOW);
  digitalWrite (STROBE_PIN, HIGH);
  delay        (1);

  digitalWrite(GREEN_MODE_PIN, HIGH);
  digitalWrite(RED_MODE_PIN, LOW);
  
  int pixelHue = random(0, 360);
  for (int i = 0; i < COLUMNS; i++ ) {
    for (int j = 0; j < ROWS; j++ ) {
      colors[i][j].r = 255;
      colors[i][j].g = 128;
      colors[i][j].b = 0;
      pixelHue += 8; 
      hsvColors[i][j] = getPackedColor(pixelHue);      
    }  
    hsvTopColors[i] = getPackedColor(pixelHue);
  }
  topColor.r = 0;
  topColor.g = 255;
  topColor.b = 0;
  
  
 }

void loop() 
  {    
    counter++;   
    
    clearspectrum(); 
    
    pulse();

    resetMSGEQ();
    
    readMSGEQValues();

    if(millis() - lastAutoChange > changeModeTime && autoMode) {
      currentMode++;
      if(currentMode > totalModes) {
        currentMode = 0;
      }
      lastAutoChange = millis();
    }

    if(currentMode != lastMode) {
      lastMode = currentMode;
      changeColorMode();
    }
    
    populateMatrix();
  
    flushMatrix();
    
    if(counter % 3 == 0){
      readInputs();
      topSinking(); //peak delay
    }
  }



  
  void topSinking()  {
    for(int j = 0; j < ROWS; j++) {
      if(peakhold[j].pointPosition > 0 && peakhold[j].peakpause <= 0){
        if(micros() - lastSync > sinkDelay) {
          lastSync = micros();
          peakhold[j].pointPosition--;
        } else if(sinkDelay == 0){
          peakhold[j].pointPosition--;
        }
      }
      else if(peakhold[j].peakpause > 0) {
        peakhold[j].peakpause--; 
      }
    } 
  }
  
  void clearspectrum()  {
    for(int i = 0; i < ROWS; i++) {
      for(int j = 0; j < COLUMNS; j++)  {
        spectrum[i][j].active = false;  
      } 
    }
  }
  
  void flushMatrix()  {
    for(int j = 0; j < COLUMNS; j++)  {
      if( j % 2 != 0)  {
        for(int i = 0; i < ROWS; i++) {
          if(spectrum[ROWS - 1 - i][j].active) {
            pixels.setPixelColor(j * ROWS + i, spectrum[ROWS - 1 - i][j].hsvColor);         
          }  
        else {
          pixels.setPixelColor( j * ROWS + i, 0, 0, 0);  
        } 
      }
    }  
    else  {
      for(int i = 0; i < ROWS; i++)  {
        if(spectrum[i][j].active)  {
          pixels.setPixelColor(j * ROWS + i, spectrum[i][j].hsvColor);      
        }
        else  {
          pixels.setPixelColor( j * ROWS + i, 0, 0, 0);  
        }
      }      
    } 
  }
  if(brightness != lastBrightness) {
    lastBrightness = brightness;
    pixels.setBrightness(brightness);
  }
  
  pixels.show();
 }

 void pulse() {
    if (millis() - pwmpulse > 3000){
      toggle = !toggle;
      digitalWrite(PULSE_PIN, toggle);
      pwmpulse = millis();
    } 
 }

 void resetMSGEQ() {
    digitalWrite(RESET_PIN, HIGH);
    //delayMicroseconds(76);
    digitalWrite(RESET_PIN, LOW);
 }

 void readMSGEQValues() {
  
    for(int i=0; i <= COLUMNS; i++){ 
      digitalWrite(STROBE_PIN, LOW);
      delayMicroseconds(30);

      spectrumValue[i] = analogRead(1);

      if(spectrumValue[i] < NOISE) {
        spectrumValue[i] = 0;
      }
      spectrumValue[i] = constrain(spectrumValue[i], 0, 1023);
      spectrumValue[i] = map(spectrumValue[i], 0, 1023, 0, ROWS);

      i++;
      spectrumValue[i] = analogRead(0);

      if(spectrumValue[i] < NOISE){
        spectrumValue[i] = 0;
      }
      spectrumValue[i] = constrain(spectrumValue[i], 0, 1023);
      spectrumValue[i] = map(spectrumValue[i], 0, 1023, 0, ROWS);
      
      digitalWrite(STROBE_PIN, HIGH);
    }
    
 }

 void populateMatrix() {
    for(int j = 0; j < COLUMNS; j++){
      for(int i = 0; i < spectrumValue[j]; i++){
        spectrum[i][COLUMNS - 1 - j].active = 1;
        spectrum[i][COLUMNS - 1 - j].r = colors[j][i].r;          //COLUMN Color red
        spectrum[i][COLUMNS - 1 - j].g = colors[j][i].g;          //COLUMN Color green
        spectrum[i][COLUMNS - 1 - j].b = colors[j][i].b;          //COLUMN Color blue
        spectrum[i][COLUMNS - 1 - j].hsvColor = hsvColors[j][i];  //COLUMN HSV color
      }
      
      if(spectrumValue[j] - 1 > peakhold[j].pointPosition) {
        spectrum[spectrumValue[j] - 1][COLUMNS - 1 - j].r = 0; 
        spectrum[spectrumValue[j] - 1][COLUMNS - 1 - j].g = 0; 
        spectrum[spectrumValue[j] - 1][COLUMNS - 1 - j].b = 0;
        spectrum[spectrumValue[j] - 1][COLUMNS - 1 - j].hsvColor = pixels.gamma32(pixels.ColorHSV(0, 0, 0));
        peakhold[j].pointPosition = spectrumValue[j] - 1;
        peakhold[j].peakpause = peakPause; //set peakpause
      }
      else {
        spectrum[peakhold[j].pointPosition][COLUMNS - 1 - j].active = 1;
        spectrum[peakhold[j].pointPosition][COLUMNS - 1 - j].r = topColor.r;              //Peak Color red
        spectrum[peakhold[j].pointPosition][COLUMNS - 1 - j].g = topColor.g;              //Peak Color green
        spectrum[peakhold[j].pointPosition][COLUMNS - 1 - j].b = topColor.b;              //Peak Color blue
        spectrum[peakhold[j].pointPosition][COLUMNS - 1 - j].hsvColor = hsvTopColors[j];  //Peak Color blue
      }
    }  
 } 


 void readInputs() {
    int pot1 = analogRead(2);
    pot1 = constrain(pot1, 0, 1023);
    peakPause = map(pot1, 0, 1023, 1, 35);

    int pot2 = analogRead(3);
    pot2 = constrain(pot2, 0, 1023);
    sinkDelay = map(pot2, 0, 1023, 0, 25);

    int pot3 = analogRead(4);
    pot3 = constrain(pot3, 0, 1023);
    brightness = map(pot3, 0, 1023, 20, 255);
 }

 void onModChange() {
    if(millis() - last_pressed < 100) {
      autoMode = !autoMode;
      digitalWrite(GREEN_MODE_PIN, autoMode ? HIGH : LOW);
      digitalWrite(RED_MODE_PIN, autoMode ? LOW : HIGH);
    } else {
      currentMode++;
      if(currentMode > totalModes) {
        currentMode = 0;
      }
    }
    last_pressed = millis();
 }

 uint32_t getPackedColor(uint16_t hue) {
    return hsl(hue, 100, 50); 
 } 

 void changeColorMode() {
  
  switch(currentMode) {
    case 1: {
      uint16_t firstPixelHue = random(0, 360);
      uint16_t pixelHue = firstPixelHue;
      for (int i = 0; i < COLUMNS; i++ ) {
        pixelHue = firstPixelHue;
        for (int j = 0; j < ROWS; j++ ) {
          if(ROWS % 4 == 0 && j > 0) {
            pixelHue = getNextColor(j, pixelHue, false);
          }
          colors[i][j].r = 128;
          colors[i][j].g = 255;
          colors[i][j].b = 0; 
          pixelHue += 9; 
          hsvColors[i][j] = getPackedColor(pixelHue);      
        }  
        hsvTopColors[i] = getPackedColor(firstPixelHue); 
      }
      topColor.r = 128;
      topColor.g = 0;
      topColor.b = 255;
      break;
    }
    case 2: {
      uint16_t firstPixelHue = random(0, 360);
      uint16_t pixelHue = 0;
      for (int i = 0; i < COLUMNS; i++ ) {
        pixelHue = firstPixelHue;
        for (int j = 0; j < ROWS; j++ ) {
          colors[i][j].r = 255;
          colors[i][j].g = 128;
          colors[i][j].b = 0; 
          pixelHue += i + j;
          hsvColors[i][j] = getPackedColor(pixelHue);       
        }
        hsvTopColors[i] = getPackedColor(firstPixelHue * 2);
      }
      topColor.r = 0;
      topColor.g = 255;
      topColor.b = 0;
      break;
    }
    case 3: {
      uint16_t firstPixelHue = random(120, 300);
      uint16_t pixelHue = 0;
      for (int i = 0; i < COLUMNS; i++ ) {
        pixelHue = firstPixelHue;
        for (int j = 0; j < ROWS; j++ ) {
          colors[i][j].r = 0;
          colors[i][j].g = 0;
          colors[i][j].b = 255;
          pixelHue += i * j * 2 / 3;
          hsvColors[i][j] = getPackedColor(pixelHue); 
        }
        hsvTopColors[i] = getPackedColor(pixelHue - 50);  
      }
      topColor.r = 255;
      topColor.g = 255;
      topColor.b = 0;
      break;
    }
    case 4: {
      uint16_t pixelHue = random(240, 310);
      for (int i = 0; i < COLUMNS; i++ ) {
        for (int j = 0; j < ROWS; j++ ) {
          colors[i][j].r = 128;
          colors[i][j].g = 0;
          colors[i][j].b = 255; 
          pixelHue += 4;
          hsvColors[i][j] = getPackedColor(pixelHue);    
        }  
        hsvTopColors[i] = getPackedColor(pixelHue + 50 / 3);
      }
      topColor.r = 128;
      topColor.g = 255;
      topColor.b = 0;
      break;
    }
    case 5: {
      int randomColor = random(0,360);
      uint16_t pixelHue = randomColor;
      for (int i = 0; i < COLUMNS; i++ ) {
        pixelHue = randomColor;
        for (int j = 0; j < ROWS; j++ ) {
          if(ROWS % 4 == 0 && j > 0) {
            pixelHue = getNextColor(j, pixelHue, false);
          }
          colors[i][j].r = 128;
          colors[i][j].g = 255;
          colors[i][j].b = 0; 
          pixelHue += 1; 
          hsvColors[i][j] = getPackedColor(pixelHue);      
        }  
        hsvTopColors[i] = getPackedColor(0); 
      }
      topColor.r = 128;
      topColor.g = 0;
      topColor.b = 255;
      break;
    }
    case 6: {
      int randomColor = random(0,360);
      uint16_t pixelHue = randomColor;
      for (int i = 0; i < COLUMNS; i++ ) {
        pixelHue = randomColor;
        for (int j = 0; j < ROWS; j++ ) {
          pixelHue = getNextColor(j, pixelHue, false);
          colors[i][j].r = 128;
          colors[i][j].g = 255;
          colors[i][j].b = 0; 
          pixelHue += 1; 
          hsvColors[i][j] = getPackedColor(pixelHue);      
        }  
        hsvTopColors[i] = getPackedColor(randomColor); 
      }
      topColor.r = 0;
      topColor.g = 0;
      topColor.b = 255;
      break;
    }
    default: {
      int pixelHue = random(0, 360);
      for (int i = 0; i < COLUMNS; i++ ) {
        for (int j = 0; j < ROWS; j++ ) {
          colors[i][j].r = 255;
          colors[i][j].g = 128;
          colors[i][j].b = 0;
          pixelHue += 8; 
          hsvColors[i][j] = getPackedColor(pixelHue);      
        }  
        hsvTopColors[i] = getPackedColor(pixelHue);
      }
      topColor.r = 0;
      topColor.g = 255;
      topColor.b = 0;
      break;
    }
  }
 }

 uint16_t getNextColor(int rowHeight, uint16_t prevColor, bool useRandom) {
    return useRandom ? random(prevColor, prevColor + 30) : prevColor + 30; // Advance 30 degree on color weel
 } 

 uint32_t hsl(uint16_t ih, uint8_t is, uint8_t il) {

  float h, s, l, t1, t2, tr, tg, tb;
  uint8_t r, g, b;

  h = (ih % 360) / 360.0;
  s = constrain(is, 0, 100) / 100.0;
  l = constrain(il, 0, 100) / 100.0;

  if ( s == 0 ) { 
    r = g = b = 255 * l;
    return ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
  } 
  
  if ( l < 0.5 ) t1 = l * (1.0 + s);
  else t1 = l + s - l * s;
  
  t2 = 2 * l - t1;
  tr = h + 1/3.0;
  tg = h;
  tb = h - 1/3.0;

  r = hsl_convert(tr, t1, t2);
  g = hsl_convert(tg, t1, t2);
  b = hsl_convert(tb, t1, t2);

  // NeoPixel packed RGB color
  return ((uint32_t)r << 16) | ((uint32_t)g <<  8) | b;
}
/**
 * HSL Convert
 * Helper function
 */
uint8_t hsl_convert(float c, float t1, float t2) {

  if ( c < 0 ) c+=1; 
  else if ( c > 1 ) c-=1;

  if ( 6 * c < 1 ) c = t2 + ( t1 - t2 ) * 6 * c;
  else if ( 2 * c < 1 ) c = t1;
  else if ( 3 * c < 2 ) c = t2 + ( t1 - t2 ) * ( 2/3.0 - c ) * 6;
  else c = t2;
  
  return (uint8_t)(c*255); 
}

 
