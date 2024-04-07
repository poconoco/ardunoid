/*
 * (c) Leonid Yurchenko
 * https://www.youtube.com/@nocomake
 */

#include <TFT.h>  
#include <SPI.h>

// TFT Screen pins
#define CS_PIN   10
#define DC_PIN   9
#define RST_PIN  8

class Color {
  public:
    Color() {}
    
    Color(int _r, int _g, int _b)
      : r(_r)
      , g(_g)
      , b(_b) 
    {}
  
    int r;
    int g;
    int b;
};

class ExtendedTFT: public TFT {
  public:
    ExtendedTFT(uint8_t CS, uint8_t RS, uint8_t RST)
      : TFT(CS, RS, RST) {}
  
    moveCircle(int16_t x0, int16_t y0, int16_t x1, int16_t y1, int16_t r, Color &_fill, Color &_stroke, Color& _bg) {
      uint16_t stroke = newColor(_stroke.r, _stroke.g, _stroke.b);
      uint16_t fill = newColor(_fill.r, _fill.g, _fill.b);
      uint16_t bg = newColor(_bg.r, _bg.g, _bg.b);

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
};

class Position {
  public:
    Position() {}
    
    Position(int _x, int _y) 
      : x(_x)
      , y(_y)
    {}

    Position operator + (Position &operand) {
      return Position(x+operand.x, y+operand.y);
    }

    void operator += (Position &operand) {
      x = x + operand.x;
      y = y + operand.y;
    }

    // Rotated coordinates
    int rX() {
      return y;
    }

    int rY() {
      return x;
    }
    
    int x;
    int y;
};

class Brick {
  public:
    Brick() : popped(false) {}
  
    Position center;
    Color color;
    bool popped;
};

class Gamefield {
  public:
    Gamefield(int width,
              int height,
              int topMargin,
              int leftMargin,
              int gap,
              int brickRadius,
              int ballRadius,
              int padWidth,
              int padHeight,
              int numRows,
              int numCols,
              int tftCSpin,
              int tftDCpin,
              int tftRSTpin)
      : _width(width)
      , _height(height)
      , _brickRadius(brickRadius)
      , _ballRadius(ballRadius)
      , _padWidth(padWidth)
      , _padHeight(padHeight)
      , _padHalfWidth(padWidth/2)
      , _padHalfHeight(padHeight/2)
      , _ballPos(width/2, height*2/3)
      , _padPos(width/2, height - _padHeight * 1.5)
      , _ballSpeed(1, -2)
      , _backgroundColor(0, 0, 0)
      , _lineColor(255, 255, 255)
      , _ballColor(0, 0, 255)
      , _padColor(255, 255, 0)
      , _screen(tftCSpin, tftDCpin, tftRSTpin)
      , _numBricks(numRows*numCols)
    {
      Color colors[] = {
        Color(255, 0, 0),
        Color(255, 255, 0),
        Color(0, 255, 255),
        Color(255, 0, 255),
        Color(0, 255, 0)
      };

      _bricks = new Brick[numRows * numCols];
      
      for (int row = 0; row < numRows; row++) {
        for (int col = 0; col < numCols; col++) {
          int x = leftMargin + (col * (brickRadius*2 + gap)) + brickRadius;
          int y = topMargin + (row * (brickRadius*2 + gap)) + brickRadius;
          int brickIndex = row*numCols + col;
          _bricks[brickIndex].center = Position(x, y); 
          _bricks[brickIndex].color = colors[row];
        }
      }
    }
    
    ~Gamefield() {
      delete _bricks;
    }

    void drawInitial() {
      _screen.begin();
      
      _screen.background(_backgroundColor.r, _backgroundColor.g, _backgroundColor.b);
      _screen.stroke(_lineColor.r, _lineColor.g, _lineColor.b);

      _screen.fill(_ballColor.r, _ballColor.g, _ballColor.b);
      _screen.circle(_ballPos.rX(), _ballPos.rY(), _ballRadius);

      _screen.fill(_ballColor.r, _ballColor.g, _ballColor.b);
      _screen.rect(_padPos.rX()-_padHalfWidth, _padPos.rY()-_padHalfHeight,
                   _padPos.rX()+_padHalfWidth, _padPos.rY()+_padHalfHeight);
  
      for (int i = 0; i < _numBricks; i++) {
        Brick &brick = _bricks[i];
        _screen.fill(brick.color.r, brick.color.g, brick.color.b);
        _screen.rect(brick.center.rX() - _brickRadius, 
                     brick.center.rY() - _brickRadius, 
                     _brickRadius*2, _brickRadius*2);
      }
    }

    void tick() {
      Position newBallPos = _ballPos + _ballSpeed;

      // Calculate walls collisions first
      if ((newBallPos.x + _ballRadius >= _width) || (newBallPos.x - _ballRadius <= 0))
        _ballSpeed.x = -_ballSpeed.x;

      if (newBallPos.y - _ballRadius <= 0)
        _ballSpeed.y = -_ballSpeed.y;

      // Then brick collisions
      for (int i = 0; i < _numBricks; i++) {
        checkBrickCollision(_bricks[i], newBallPos);                
      }

      // Then pad collision
      checkPadCollision();

      // Check ball flew out of the bottom
      if (newBallPos.y - _ballRadius >= _height) {
        // TODO: decrement pads, wait for click
        _ballPos = Position(_width/2, _height*2/3);
        _ballSpeed = Position(0, -2); 
      }
      
      // But actually move ball using new speed after collision
      Position oldBallPos = _ballPos;
      _ballPos += _ballSpeed;  
      _screen.moveCircle(oldBallPos.rX(), oldBallPos.rY(), 
                         _ballPos.rX(), _ballPos.rY(), 
                         _ballRadius, _ballColor, _lineColor, _backgroundColor);
    }

    void checkBrickCollision(Brick &brick, Position &ballPos) {
      if (brick.popped)
        return;
      
      if (abs(brick.center.x - ballPos.x) < _brickRadius + _ballRadius &&
          abs(brick.center.y - ballPos.y) < _brickRadius + _ballRadius) {
            
        // Collision
        popBrick(brick);

        // Ball coordinates relative to brick center
        int relBallX = ballPos.x - brick.center.x;
        int relBallY = ballPos.y - brick.center.y;

        // Determine collision side
        if (abs(relBallX) > abs(relBallY)) {
          // Left or right collision
          _ballSpeed.x = -_ballSpeed.x;
        } else 
        {
          // Top or Bottom collision
          _ballSpeed.y = -_ballSpeed.y;
        }
      }
    }

    void checkPadCollision() {
      // Higher than touching a pad
      if (_ballPos.y < _padPos.y-_padHalfHeight-_ballRadius)
        return;
      
      // Top pad section
      if (_ballPos.x >= _padPos.x-_padHalfWidth &&
          _ballPos.x <= _padPos.x+_padHalfWidth) {
        _ballSpeed.y = -_ballSpeed.y;
        return;
      }

      
    }

    void popBrick(Brick &brick) {
      // Draw background
      _screen.stroke(_backgroundColor.r, _backgroundColor.g, _backgroundColor.b);
      _screen.fill(_backgroundColor.r, _backgroundColor.g, _backgroundColor.b);
      _screen.rect(brick.center.rX() - _brickRadius, 
                   brick.center.rY() - _brickRadius, 
                   _brickRadius*2, _brickRadius*2);
      // Mark popped
      brick.popped = true;
    }
  
  private:
    Brick *_bricks;
    Position _ballPos;
    Position _padPos;
    Position _ballSpeed;
    int _width;
    int _height;
    int _brickRadius;
    int _ballRadius;
    int _padWidth;
    int _padHeight;
    int _padHalfWidth;
    int _padHalfHeight;
    int _numBricks;
    Color _backgroundColor;
    Color _lineColor;
    Color _ballColor;
    Color _padColor;
    ExtendedTFT _screen;
};


Gamefield gamefield(
  128, // screen width
  160, // screen height
  5,   // topMargin,
  5,   // leftMargin,
  2,   // gap,
  5,   // brickRadius,
  3,   // ballRadius,
  10,  // padWidth
  4,   // padHeight
  5,   // numRows,
  10,  // numCols,
  CS_PIN,   // tftCSpin,
  DC_PIN,   // tftDCpin,
  RST_PIN);  // tftRSTpin

void setup() {
  gamefield.drawInitial();
}

void loop() {
  gamefield.tick();
  delay(10);
}
