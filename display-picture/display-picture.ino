#include <Arduino.h>
#include <RGBConverter.h>
RGBConverter rgb;

// MCUFRIEND UNO shields have microSD on pins 10, 11, 12, 13
// The official <SD.h> library only works on the hardware SPI pins
// e.g. 11, 12, 13 on a Uno  (or STM32 Nucleo)
//
// copy all your BMP files to the root directory on the microSD with your PC
// (or another directory)
#include <SPI.h>            // f.k. for Arduino-1.5.2
#include <Wire.h>
#define wireClockSpeed 4000000

#include <TouchScreen.h>
#include <Adafruit_GFX.h>   // Hardware-specific library
#include <MCUFRIEND_kbv.h>
#include <SdFat.h>             // Use the official SD library on hardware pins
#define SD_CS     10
SdFat SD;
MCUFRIEND_kbv tft;
#define MINPRESSURE 200
#define MAXPRESSURE 1000

// ALL Touch panels and wiring is DIFFERENT
// copy-paste results from TouchScreen_Calibr_native.ino
const int XP=8,XM=A2,YP=A3,YM=9; //240x320 ID=0x9341
const int TS_LEFT=110,TS_RT=923,TS_TOP=72,TS_BOT=889;
TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

int pixel_x, pixel_y;     //Touch_getXY() updates global vars
bool touched = false;
bool Touch_getXY(void)
{
    TSPoint p = ts.getPoint();
    pinMode(YP, OUTPUT);      //restore shared pins
    pinMode(XM, OUTPUT);
    digitalWrite(YP, HIGH);   //because TFT control pins
    digitalWrite(XM, HIGH);
    bool pressed = (p.z > MINPRESSURE && p.z < MAXPRESSURE);
    if (pressed) {
        pixel_x = map(p.x, TS_LEFT, TS_RT, 0, tft.width()); //.kbv makes sense to me
        pixel_y = map(p.y, TS_TOP, TS_BOT, 0, tft.height());
    }
    return pressed;
}


#define PALETTEDEPTH   0     // do not support Palette modes

void onReceive(int numBytes = 0);
void onRequest(int numBytes);

enum Process {
  touchWait,
  touchDown,
  touchUp,
  start,
  header,
  pixels,
  footer
} process = Process::touchWait;


void loop() {


  if (process == Process::touchWait) {
    if (Touch_getXY()){
      process = Process::touchDown;
      //Serial.println("touchDown");
    }
  }
  else if (process == Process::touchDown) {
    if (!Touch_getXY()) {
      process = Process::touchUp;
      //Serial.println("touchUp");
    }
  }
  
  onReceive();
  
}

void setup()
{
    uint16_t ID;
    Serial.begin(115200);

    Serial.print(F("Show BMP files on TFT with ID:0x"));
    ID = tft.readID();
    tft.begin(ID);
    Serial.println(ID, HEX);
    
    bool good = SD.begin(SD_CS);
    if (!good) {
        Serial.print(F("cannot start SD"));
        while (1)
          ;
    }
    showBMP("/bee.bmp", 0, 0);

    Wire.setClock(wireClockSpeed);
    Wire.onRequest(onRequest);
    Wire.onReceive(onReceive);
    // join i2c bus with address #8
    Wire.begin(0x08);
    Serial.println(F("Wire set up as slave at address 0x08"));

}


typedef struct  {
    uint8_t signature[2];
    uint32_t filesize;
    uint32_t reserved;
    uint32_t fileoffset_to_pixelarray;
} fileheader;

typedef struct {
    uint32_t dibheadersize;
    uint32_t width;
    uint32_t height;
    uint16_t planes;
    uint16_t bitsperpixel;
    uint32_t compression;
    uint32_t imagesize;
    uint32_t ypixelpermeter;
    uint32_t xpixelpermeter;
    uint32_t numcolorspallette;
    uint32_t mostimpcolor;
}  bitmapinfoheader;

typedef struct {
   fileheader fileheader;
   bitmapinfoheader bitmapinfoheader;
} bitmap;

#define BITMAP_HEADER_SIZE 54

uint32_t bytesRead = 0;
bitmap  imageHeader;
uint8_t* imageHeaderPointer = (uint8_t*)&imageHeader;
uint8_t buffer[4];
size_t byteIndex = 0;
uint32_t pixelSize = 0;
uint32_t pixelIndex = 0;
uint32_t pixelLength = 0;
int16_t x;
int16_t y;


void onRequest(int numBytes) {

  if (process == Process::touchUp) {
    process = Process::start;
    Wire.write((uint8_t)process);
  }
  else {
    Wire.write((uint8_t)process);
  }
}

void onReceive(int numBytes) {
  while (Wire.available() > 0) {
    uint8_t b = Wire.read();
    ++bytesRead;
    if (bytesRead % 1000 == 0)
      Serial.print(".");
    if (bytesRead % 100000 == 0)
      Serial.println();

    switch (process) {
      case Process::start:
        Serial.println("Reading image ");
        bytesRead = 1;
        byteIndex = 0;
        pixelIndex = 0;
        x = 0;
        y = 0;
        process = Process::header;
      case Process::header:
        imageHeaderPointer[byteIndex++] = b;
        if (byteIndex == BITMAP_HEADER_SIZE) {
          Serial.print("Size of bitmap: ");
          Serial.println(sizeof(bitmap));
          Serial.print("Signature ");
          Serial.print((char)(imageHeader.fileheader.signature[0]));
          Serial.println((char)(imageHeader.fileheader.signature[1]));

          Serial.print(F("File size "));
          Serial.println(imageHeader.fileheader.filesize);
          Serial.print(F("Image size ("));
          Serial.print(imageHeader.bitmapinfoheader.width);
          Serial.print(F(", "));
          Serial.print(-imageHeader.bitmapinfoheader.height);
          Serial.print(F(")"));
          Serial.print(F("."));

          pixelSize = imageHeader.bitmapinfoheader.bitsperpixel / 8;
          pixelLength =  imageHeader.bitmapinfoheader.imagesize / pixelSize;

          Serial.print(pixelLength);
          Serial.print(F("/"));
          Serial.println(pixelSize);
          process = Process::pixels;
          byteIndex = 0;
          tft.setAddrWindow(0, 0, imageHeader.bitmapinfoheader.width, -imageHeader.bitmapinfoheader.height);
        }
        break;
      case Process::pixels:
        buffer[byteIndex++] = b;
        if (byteIndex == pixelSize) {


          uint8_t b = buffer[0];
          uint8_t g = buffer[1];
          uint8_t r = buffer[2];
/*
          double hsl[3];

          rgb.rgbToHsl(r, g, b, hsl);

          hsl[1] = 0.7;
          
          rgb.hslToRgb(hsl[0], hsl[1], hsl[2], buffer);

          r = buffer[0];
          g = buffer[1];
          b = buffer[2];
*/          
          uint32_t pixel = tft.color565(r,g,b);

          //uint8_t c0 = buffer[0];
          //uint8_t c1 = buffer[1];
          //buffer[0]=  c1;
          //buffer[1] = c0;
          //uint32_t pixel = *buffer;

          tft.writePixel(y, x, pixel);
          byteIndex = 0;
          
          if (++x == imageHeader.bitmapinfoheader.width) {
            x = 0;
            if (++y == -imageHeader.bitmapinfoheader.height) {
              if (bytesRead == imageHeader.fileheader.filesize) {
                Serial.println("Read Image Ok");
              }
              else {
                Serial.println("Error reading image");
              }
              process = Process::touchWait;
              byteIndex = 0;
              //Wire.onRequest(onRequest);
            }
          }
        }
    }
  }
}

#define BMPIMAGEOFFSET 54

#define BUFFPIXEL      20

uint16_t read16(File& f) {
    uint16_t result;         // read little-endian
    f.read((uint8_t*)&result, sizeof(result));
    return result;
}

uint32_t read32(File& f) {
    uint32_t result;
    f.read((uint8_t*)&result, sizeof(result));
    return result;
}

uint8_t showBMP(char *nm, int x, int y)
{
    File bmpFile;
    int bmpWidth, bmpHeight;    // W+H in pixels
    uint8_t bmpDepth;           // Bit depth (currently must be 24, 16, 8, 4, 1)
    uint32_t bmpImageoffset;    // Start of image data in file
    uint32_t rowSize;           // Not always = bmpWidth; may have padding
    uint8_t sdbuffer[3 * BUFFPIXEL];    // pixel in buffer (R+G+B per pixel)
    uint16_t lcdbuffer[(1 << PALETTEDEPTH) + BUFFPIXEL], *palette = NULL;
    uint8_t bitmask, bitshift;
    boolean flip = true;        // BMP is stored bottom-to-top
    int w, h, row, col, lcdbufsiz = (1 << PALETTEDEPTH) + BUFFPIXEL, buffidx;
    uint32_t pos;               // seek position
    boolean is565 = false;      //

    uint16_t bmpID;
    uint16_t n;                 // blocks read
    uint8_t ret;

    if ((x >= tft.width()) || (y >= tft.height()))
        return 1;               // off screen

    bmpFile = SD.open(nm);      // Parse BMP header
    bmpID = read16(bmpFile);    // BMP signature
    (void) read32(bmpFile);     // Read & ignore file size
    (void) read32(bmpFile);     // Read & ignore creator bytes
    bmpImageoffset = read32(bmpFile);       // Start of image data
    (void) read32(bmpFile);     // Read & ignore DIB header size
    bmpWidth = read32(bmpFile);
    bmpHeight = read32(bmpFile);
    n = read16(bmpFile);        // # planes -- must be '1'
    bmpDepth = read16(bmpFile); // bits per pixel
    pos = read32(bmpFile);      // format
    if (bmpID != 0x4D42) ret = 2; // bad ID
    else if (n != 1) ret = 3;   // too many planes
    else if (pos != 0 && pos != 3) ret = 4; // format: 0 = uncompressed, 3 = 565
    else if (bmpDepth < 16 && bmpDepth > PALETTEDEPTH) ret = 5; // palette 
    else {
        bool first = true;
        is565 = (pos == 3);               // ?already in 16-bit format
        // BMP rows are padded (if needed) to 4-byte boundary
        rowSize = (bmpWidth * bmpDepth / 8 + 3) & ~3;
        if (bmpHeight < 0) {              // If negative, image is in top-down order.
            bmpHeight = -bmpHeight;
            flip = false;
        }

        w = bmpWidth;
        h = bmpHeight;
        if ((x + w) >= tft.width())       // Crop area to be loaded
            w = tft.width() - x;
        if ((y + h) >= tft.height())      //
            h = tft.height() - y;

        if (bmpDepth <= PALETTEDEPTH) {   // these modes have separate palette
            //bmpFile.seek(BMPIMAGEOFFSET); //palette is always @ 54
            bmpFile.seek(bmpImageoffset - (4<<bmpDepth)); //54 for regular, diff for colorsimportant
            bitmask = 0xFF;
            if (bmpDepth < 8)
                bitmask >>= bmpDepth;
            bitshift = 8 - bmpDepth;
            n = 1 << bmpDepth;
            lcdbufsiz -= n;
            palette = lcdbuffer + lcdbufsiz;
            for (col = 0; col < n; col++) {
                pos = read32(bmpFile);    //map palette to 5-6-5
                palette[col] = ((pos & 0x0000F8) >> 3) | ((pos & 0x00FC00) >> 5) | ((pos & 0xF80000) >> 8);
            }
        }

        // Set TFT address window to clipped image bounds
        tft.setAddrWindow(x, y, x + w - 1, y + h - 1);
        for (row = 0; row < h; row++) { // For each scanline...
            // Seek to start of scan line.  It might seem labor-
            // intensive to be doing this on every line, but this
            // method covers a lot of gritty details like cropping
            // and scanline padding.  Also, the seek only takes
            // place if the file position actually needs to change
            // (avoids a lot of cluster math in SD library).
            uint8_t r, g, b, *sdptr;
            int lcdidx, lcdleft;
            if (flip)   // Bitmap is stored bottom-to-top order (normal BMP)
                pos = bmpImageoffset + (bmpHeight - 1 - row) * rowSize;
            else        // Bitmap is stored top-to-bottom
                pos = bmpImageoffset + row * rowSize;
            if (bmpFile.position() != pos) { // Need seek?
                bmpFile.seek(pos);
                buffidx = sizeof(sdbuffer); // Force buffer reload
            }

            for (col = 0; col < w; ) {  //pixels in row
                lcdleft = w - col;
                if (lcdleft > lcdbufsiz) lcdleft = lcdbufsiz;
                for (lcdidx = 0; lcdidx < lcdleft; lcdidx++) { // buffer at a time
                    uint16_t color;
                    // Time to read more pixel data?
                    if (buffidx >= sizeof(sdbuffer)) { // Indeed
                        bmpFile.read(sdbuffer, sizeof(sdbuffer));
                        buffidx = 0; // Set index to beginning
                        r = 0;
                    }
                    switch (bmpDepth) {          // Convert pixel from BMP to TFT format
                        case 24:
                            b = sdbuffer[buffidx++];
                            g = sdbuffer[buffidx++];
                            r = sdbuffer[buffidx++];
                            color = tft.color565(r, g, b);
                            break;
                        case 16:
                            b = sdbuffer[buffidx++];
                            r = sdbuffer[buffidx++];
                            if (is565)
                                color = (r << 8) | (b);
                            else
                                color = (r << 9) | ((b & 0xE0) << 1) | (b & 0x1F);
                            break;
                        case 1:
                        case 4:
                        case 8:
                            if (r == 0)
                                b = sdbuffer[buffidx++], r = 8;
                            color = palette[(b >> bitshift) & bitmask];
                            r -= bmpDepth;
                            b <<= bmpDepth;
                            break;
                    }
                    lcdbuffer[lcdidx] = color;

                }
                tft.pushColors(lcdbuffer, lcdidx, first);
                first = false;
                col += lcdidx;
            }           // end cols
        }               // end rows
        tft.setAddrWindow(0, 0, tft.width() - 1, tft.height() - 1); //restore full screen
        ret = 0;        // good render
    }
    bmpFile.close();
    return (ret);
}
