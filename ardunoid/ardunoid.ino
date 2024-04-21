/*
 * (c) Leonid Yurchenko
 * https://www.youtube.com/@nocomake
 */

#include <TFT.h>  
#include <SPI.h>

// TFT Screen pins
#define CS_PIN   6
#define DC_PIN   7
#define RST_PIN  5

// Joystick pins
#define JOY_X_PIN 0
#define JOY_Y_PIN 1

#define JOY_X_DIR -1  // 1 or -1

#define SPEAKER_PIN 8

class ExtendedTFT: public TFT {
  public:
    ExtendedTFT(uint8_t CS, uint8_t RS, uint8_t RST)
      : TFT(CS, RS, RST) {}
  
    moveCircle(int16_t x0, int16_t y0, 
               int16_t x1, int16_t y1, 
               int16_t r, 
               uint16_t fill, 
               uint16_t stroke, 
               uint16_t bg) {

      int r2 = r * r + 1;
      int dx = x1 - x0;
      int dy = y1 - y0;
      // Clean prev circle, except new
      for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
          if (x*x + y*y > r2)
            continue;  // Outside of old circle

          if ((x-dx)*(x-dx) + (y-dy)*(y-dy) <= r2)
            continue;  // Inside new circle

          drawPixel(x0+x, y0+y, bg);
        }
      }

      fillCircle(x1, y1, r, fill);
      drawCircle(x1, y1, r, stroke);      
    }

  moveRoundRect(int16_t x0, int16_t y0, 
                int16_t x1, int16_t y1,
                int16_t w, int16_t h,
                int16_t r,
                uint16_t fill, 
                uint16_t stroke, 
                uint16_t bg) {
      int xRedrawX;
      int xRedrawY;
      int xRedrawWidth;
      int xRedrawHeight;
      int yRedrawX;
      int yRedrawY;
      int yRedrawWidth;
      int yRedrawHeight;
      if (x1 > x0) {
        xRedrawX = x0;
        xRedrawWidth = x1 - x0 + r;
        yRedrawX = x0;
        yRedrawWidth = w;
      } else {
        xRedrawX = x1 + w - r;
        xRedrawWidth = x0 - x1 + r;
        yRedrawX = x0;
        yRedrawWidth = w;
      }

      if (y1 > y0) {
        xRedrawY = y0;
        xRedrawHeight = h - (y1 - y0);
        yRedrawY = y1 - h - r;
        yRedrawHeight = y1 - y0 + r;
      } else {
        xRedrawY = y1;
        xRedrawHeight = h - (y0 - y1);
        yRedrawY = y0;
        yRedrawHeight = y0 - y1 + r;
      }

      xRedrawWidth = min(xRedrawWidth, w);
      xRedrawHeight = min(xRedrawHeight, h);
      yRedrawWidth = min(yRedrawWidth, w);
      yRedrawHeight = min(yRedrawHeight, h);

      if (x0 != x1 && xRedrawWidth > 0 && xRedrawHeight > 0)
        fillRect(xRedrawX, xRedrawY, xRedrawWidth, xRedrawHeight, bg);

      if (y0 != y1 && yRedrawWidth > 0 && yRedrawHeight > 0)
        fillRect(yRedrawX, yRedrawY, yRedrawWidth, yRedrawHeight, bg);

      fillRoundRect(x1, y1, w, h, r, fill);
      drawRoundRect(x1, y1, w, h, r, stroke);
  }
};

struct Vector {
  public:
    Vector()
      : x(0)
      , y(0) 
    {}
    
    Vector(int _x, int _y) 
      : x(_x)
      , y(_y)
    {}

    Vector operator + (Vector &operand) {
      return Vector(x+operand.x, y+operand.y);
    }

    void operator += (Vector &operand) {
      x = x + operand.x;
      y = y + operand.y;
    }

    int x;
    int y;
};

struct Brick {
  Brick()
    : popped(false)
  {}

  Vector center;
  bool popped;
};

class Gamefield {
  public:
    Gamefield(int width,
              int height,
              int topMargin,
              int leftMargin,
              int gap,
              int brickWidth,
              int ballRadius,
              int padWidth,
              int padHeight,
              int numRows,
              int numCols)
      : _width(width)
      , _height(height)
      , _brickWidth(brickWidth)
      , _brickHeight(brickWidth / 2)
      , _ballRadius(ballRadius)
      , _padWidth(padWidth)
      , _padHeight(padHeight)
      , _padHalfWidth(padWidth/2)
      , _padHalfHeight(padHeight/2)
      , _ballPos(width/2, height*2/3)
      , _padPos(width/2, height - padHeight * 1.2)
      , _prevPadPos(width/2, height - padHeight * 1.2)
      , _ballSpeed(1, -2)
      , _screen(CS_PIN, DC_PIN, RST_PIN)
      , _numBricks(numRows*numCols)
      , _numCols(numCols)
      , _numRows(numRows)
    {

      _bgColor = _screen.newColor(0, 0, 0);
      _strokeColor = _screen.newColor(255, 255, 255);
      _ballColor = _screen.newColor(0, 0, 255);
      _padColor = _screen.newColor(255, 255, 0);

      _bricks = new Brick[_numBricks];
      
      for (int row = 0; row < numRows; row++) {
        for (int col = 0; col < numCols; col++) {
          int brickIndex = row*numCols + col;
          _bricks[brickIndex].center.x = leftMargin + (col * (_brickWidth + gap)) + _brickWidth / 2;
          _bricks[brickIndex].center.y = topMargin + (row * (_brickHeight + gap)) + _brickHeight / 2;
        }
      }
    }
    
    ~Gamefield() {
      delete _bricks;
    }

    void drawInitial() {
      _screen.begin();
      _screen.setRotation(0);
      
      _screen.background(_bgColor);
      _screen.stroke(_strokeColor);

      _screen.fill(_ballColor);
      _screen.circle(_ballPos.x, _ballPos.y, _ballRadius);

      drawPad();

      uint16_t brickColors[] = {
        _screen.newColor(255, 0, 0),
        _screen.newColor(255, 255, 0),
        _screen.newColor(0, 255, 255),
        _screen.newColor(255, 0, 255),
        _screen.newColor(0, 255, 0)
      };

      for (int i = 0; i < _numBricks; i++) {
        Brick &brick = _bricks[i];
        _screen.fill(brickColors[(i / _numCols) % 5]);
        _screen.rect(brick.center.x - _brickWidth / 2, 
                     brick.center.y - _brickHeight / 2, 
                     _brickWidth, _brickHeight);
      }
    }

    void drawPad() {
      _screen.moveRoundRect(_prevPadPos.x-_padHalfWidth, _prevPadPos.y-_padHalfHeight, 
                            _padPos.x-_padHalfWidth, _padPos.y-_padHalfHeight, 
                            _padWidth, _padHeight,
                            _padHeight / 2,
                            _padColor,
                            _strokeColor,
                            _bgColor);      

      _prevPadPos = _padPos;
    }

    void tick() {
      Vector potentialBallPos = _ballPos + _ballSpeed;
      readPadSpeed();

      // Calculate walls collisions first
      if ((potentialBallPos.x + _ballRadius >= _width) || (potentialBallPos.x - _ballRadius <= 0)) {
        _ballSpeed.x = -_ballSpeed.x;
        tone(SPEAKER_PIN, 300, 10);
      }

      if (potentialBallPos.y - _ballRadius <= 0) {
        _ballSpeed.y = -_ballSpeed.y;
        tone(SPEAKER_PIN, 300, 10);
      }

      // Then brick collisions
      for (int i = 0; i < _numBricks; i++) {
        if (checkBrickCollision(_bricks[i], potentialBallPos))
          tone(SPEAKER_PIN, 500, 10);
      }

      // Then pad collision
      if (checkPadCollision())
        tone(SPEAKER_PIN, 150, 50);

      // Check ball flew out of the bottom
      if (potentialBallPos.y - _ballRadius >= _height) {
        // TODO: decrement pads, wait for click
        ballOut();
      }

      moveBall(_ballPos + _ballSpeed);
      movePad(_padPos + _padSpeed);
    }

    void readPadSpeed() {
        int joyX = analogRead(JOY_X_PIN);
        _padSpeed.x = map(joyX, 0, 1023, -5 * JOY_X_DIR, 6 * JOY_X_DIR);
    }

    void moveBall(Vector newPos) {
      // But actually move ball using new speed after collision
      Vector oldBallPos = _ballPos;
      _ballPos = newPos;  
      _screen.moveCircle(oldBallPos.x, oldBallPos.y, 
                         _ballPos.x, _ballPos.y, 
                         _ballRadius, _ballColor, _strokeColor, _bgColor);

    }

    void movePad(Vector newPos) {
      if (newPos.x - _padHalfWidth >= 0 && newPos.x + _padHalfWidth < _width)
        _padPos = newPos;

      drawPad();    
    }

    bool checkBrickCollision(Brick &brick, Vector &ballPos) {
      if (brick.popped)
        return false;
      
      if (abs(brick.center.x - ballPos.x) <= _brickWidth / 2 + _ballRadius &&
          abs(brick.center.y - ballPos.y) <= _brickHeight / 2 + _ballRadius) {
            
        // Collision
        popBrick(brick);

        // Ball coordinates relative to brick center
        int relBallX = ballPos.x - brick.center.x + _brickWidth / 2;
        int relBallY = ballPos.y - brick.center.y + _brickHeight / 2;

        // Determine collision side
        if (abs(relBallX) > abs(relBallY)) {
          // Left or right collision
          _ballSpeed.x = -_ballSpeed.x;
        } else 
        {
          // Top or Bottom collision
          _ballSpeed.y = -_ballSpeed.y;
        }

        return true;
      }

      return false;
    }

    bool checkPadCollision() {
      // Already flying up (may be after low collision with pad)
      if (_ballSpeed.y < 0)
        return false;

      // Higher than touching a pad
      if (_ballPos.y + _ballRadius < _padPos.y - _padHalfHeight)
        return false;
      
      // Too low to pick up
      if (_ballPos.y > _padPos.y)
        return false;

      // To allow more area for side collision
      const int smallerHalfWidth = _padHalfWidth * 0.8;
      const int sideKickBallSpeed = 1;

      // Top pad section
      if (_ballPos.x >= _padPos.x - smallerHalfWidth &&
          _ballPos.x <= _padPos.x + smallerHalfWidth) {
        _ballSpeed.y = -_ballSpeed.y;
        _ballSpeed.x = _ballSpeed.x >> 1;
        _ballSpeed.x += _padSpeed.x >> 1;

        return true;
      }

      // Left side kick
      if (_ballPos.x < _padPos.x-smallerHalfWidth &&
          _ballPos.x + _ballRadius >= _padPos.x - _padHalfWidth) {

        _ballSpeed.x = - sideKickBallSpeed;
        _ballSpeed.y = - _ballSpeed.y;

        return true;
      }

      // Right side kick
      if (_ballPos.x > _padPos.x+smallerHalfWidth &&
          _ballPos.x - _ballRadius <= _padPos.x + _padHalfWidth) {

        _ballSpeed.x = sideKickBallSpeed;
        _ballSpeed.y = - _ballSpeed.y;

        return true;
      }


    }

    void popBrick(Brick &brick) {
      // Draw background
      _screen.stroke(_bgColor);
      _screen.fill(_bgColor);
      _screen.rect(brick.center.x - _brickWidth / 2, 
                   brick.center.y - _brickHeight / 2, 
                   _brickWidth, _brickHeight);
      // Mark popped
      brick.popped = true;
    }

    void ballOut() {
      tone(SPEAKER_PIN, 300, 200);
      delay(200);
      tone(SPEAKER_PIN, 200, 200);
      delay(200);

      _ballSpeed = Vector(0, -2);
      moveBall(Vector(_width/2, _height*2/3));
    }
  
  private:
    Brick *_bricks;
    Vector _ballPos;
    Vector _ballSpeed;
    Vector _padPos;
    Vector _padSpeed;
    Vector _prevPadPos;
    int _width;
    int _height;
    const int _brickWidth;
    const int _brickHeight;
    int _ballRadius;
    int _padWidth;
    int _padHeight;
    int _padHalfWidth;
    int _padHalfHeight;
    int _numBricks;
    int _numRows;
    int _numCols;
    uint16_t _bgColor;
    uint16_t _strokeColor;
    uint16_t _ballColor;
    uint16_t _padColor;
    ExtendedTFT _screen;
    int _joyXPin;
    int _joyYPin;
};


Gamefield gamefield(
  128, // gamefield width
  160, // gamefield height
  5,   // top margin,
  5,   // left margin,
  2,   // gap,
  10,   // brick radius,
  3,   // ball radius,
  20,  // pad width
  4,   // pad height
  10,   // num rows,
  10);
  
void setup() {
  gamefield.drawInitial();
}

void loop() {
  gamefield.tick();
  delay(10);
}
