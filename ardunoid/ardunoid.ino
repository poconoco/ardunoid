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

// Joystick pins
#define JOY_X_PIN 0
#define JOY_Y_PIN 1

class Color {
  public:
    Color() {}

    Color(const Color &other) 
      : r(other.r)
      , g(other.g)
      , b(other.b) 
    {}
    
    Color(uint8_t _r, uint8_t _g, uint8_t _b)
      : r(_r)
      , g(_g)
      , b(_b) 
    {}
  
    uint8_t r;
    uint8_t g;
    uint8_t b;
};

class ExtendedTFT: public TFT {
  public:
    ExtendedTFT(uint8_t CS, uint8_t RS, uint8_t RST)
      : TFT(CS, RS, RST) {}
  
    moveCircle(int16_t x0, int16_t y0, 
               int16_t x1, int16_t y1, 
               int16_t r, 
               Color &_fill, 
               Color &_stroke, 
               Color &_bg) {
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

  moveRoundRect(int16_t x0, int16_t y0, 
                int16_t x1, int16_t y1,
                int16_t w, int16_t h,
                int16_t r,
                Color &_fill, 
                Color &_stroke, 
                Color &_bg) {
      uint16_t stroke = newColor(_stroke.r, _stroke.g, _stroke.b);
      uint16_t fill = newColor(_fill.r, _fill.g, _fill.b);
      uint16_t bg = newColor(_bg.r, _bg.g, _bg.b);

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

class Vector {
  public:
    Vector()
      : x(0)
      , y(0) 
    {}

    Vector(const Vector &other) 
      : x(other.x)
      , y(other.y)
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

    void setOffScreen() {
      x = -1000;
      y = -1000;
    }
    
    int x;
    int y;
};

class GamefieldObject {
  public:
    GamefieldObject(int x, int y, ExtendedTFT& screen, Color fillColor, Color strokeColor, Color bgColor) 
      : center(x, y)
      , speed(0, 0)
      , _screen(screen)
      , _color(fillColor)
      , _strokeColor(strokeColor)
      , _bgColor(bgColor)
    {}

    void move() {
      center += speed;
    }

    virtual void draw() = 0;
    virtual void clear() = 0;

    Vector center;
    Vector speed;

  protected:
    ExtendedTFT& _screen;
    Color _color;
    Color _strokeColor;
    Color _bgColor;
};

class Ball : public GamefieldObject {
  public:
    Ball(ExtendedTFT& screen, 
         int x, 
         int y, 
         int r, 
         Color fillColor, 
         Color strokeColor, 
         Color bgColor,
         int fieldWidth,
         int fieldHeight)
      : GamefieldObject(x, y, screen, fillColor, strokeColor, bgColor)
      , radius(r)
      , _initialCenter(x, y)
      , _fieldWidth(fieldWidth)
      , _fieldHeight(fieldHeight)
    {}

    void reset() {
      speed.x = 1;
      speed.y = -2;

      center = _initialCenter;
    }

    virtual void draw() {
      _screen.moveCircle(_prevCenter.x, _prevCenter.y, 
                         center.x, center.y, 
                         radius, _color, _strokeColor, _bgColor);
      _prevCenter = center;
    }

    virtual void clear() {
      _screen.stroke(_bgColor.r, _bgColor.g, _bgColor.b);
      _screen.fill(_bgColor.r, _bgColor.g, _bgColor.b);
      _screen.circle(center.x, center.y, radius);
      _prevCenter.setOffScreen();
    }

    void wallCollision() {
      Vector potentialBallPos = center + speed;

      if ((potentialBallPos.x + radius >= _fieldWidth) || (potentialBallPos.x - radius <= 0))
        speed.x *= -1;

      if (potentialBallPos.y - radius <= 0)
        speed.y *= -1;

    }

    bool isOut() {
      return center.y + radius >= _fieldHeight;
    }

    int radius;

  private:
    int _fieldWidth;
    int _fieldHeight;
    Vector _prevCenter;
    Vector _initialCenter;
};

class BallCollidable : public GamefieldObject {
  public:
    BallCollidable(int x, int y, ExtendedTFT& screen, Color fillColor, Color strokeColor, Color bgColor)
      : GamefieldObject(x, y, screen, fillColor, strokeColor, bgColor)
    {}

    virtual bool collision(Ball &ball) = 0;
};

class Brick : public BallCollidable {
  public:
    Brick(ExtendedTFT& screen, int x, int y, int w, int h, Color fillColor, Color strokeColor, Color bgColor) 
      : BallCollidable(x, y, screen, fillColor, strokeColor, bgColor)
      , width(w)
      , height(h)
      , _popped(false)
    {}
  
    virtual void draw() {
      _screen.stroke(_strokeColor.r, _strokeColor.g, _strokeColor.b);
      _screen.fill(_color.r, _color.g, _color.b);
      _screen.rect(center.x - width >> 1, 
                   center.y - height >> 1, 
                   width, height);      
    }

    virtual void clear() {
      _screen.stroke(_bgColor.r, _bgColor.g, _bgColor.b);
      _screen.fill(_bgColor.r, _bgColor.g, _bgColor.b);
      _screen.rect(center.x - width >> 1, 
                   center.y - height >> 1, 
                   width, height);

    }

    virtual bool collision(Ball &ball) {
      if (_popped)
        return;
      
      if (abs(center.x - ball.center.x) < width >> 1 + ball.radius &&
          abs(center.y - ball.center.y) < height >> 1 + ball.radius) {
            
        // Collision
        _popped = true;
        clear();

        // Ball coordinates relative to brick center
        int relBallX = ball.center.x - center.x - width >> 1;
        int relBallY = ball.center.y - center.y - height >> 1;

        // Determine collision side
        if (abs(relBallX) > abs(relBallY)) {
          // Left or right collision
          ball.speed.x *= -1;
        } else 
        {
          // Top or Bottom collision
          ball.speed.y *= -1;
        }
      }
    }

  private:
    int width;
    int height;
    bool _popped;

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
              int numCols,
              int tftCSpin,
              int tftDCpin,
              int tftRSTpin,
              int joyXPin,
              int joyYPin)
      : _width(width)
      , _height(height)
      , _padWidth(padWidth)
      , _padHeight(padHeight)
      , _padHalfWidth(padWidth/2)
      , _padHalfHeight(padHeight/2)
      , _padPos(width/2, height - padHeight * 1.2)
      , _prevPadPos(width/2, height - padHeight * 1.2)
      , _backgroundColor(0, 0, 0)
      , _lineColor(255, 255, 255)
      , _padColor(255, 255, 0)
      , _screen(tftCSpin, tftDCpin, tftRSTpin)
      , _numBricks(numRows*numCols)
      , _joyXPin(joyXPin)
      , _joyYPin(joyYPin)
      , _ball(_screen, width/2, height*2/3, ballRadius, Color(0, 0, 255), _lineColor, _backgroundColor, width, height)
    {
      _ball.reset();

      Color colors[] = {
        Color(255, 0, 0),
        Color(255, 255, 0),
        Color(0, 255, 255),
        Color(255, 0, 255),
        Color(0, 255, 0)
      };

      _bricks = new Brick*[numRows * numCols];
      int brickHeight = brickWidth >> 1;

      Serial.print("Starting...\n");

      for (int row = 0; row < numRows; row++) {
        for (int col = 0; col < numCols; col++) {
          int x = leftMargin + (col * (brickWidth + gap)) + brickWidth >> 1;
          int y = topMargin + (row * (brickHeight + gap)) + brickHeight >> 1;
          int brickIndex = row * numCols + col;
          Color &color = colors[row % 5];
          const Brick *test = new Brick(_screen, x, y, brickWidth, brickHeight, color, color, color);
          //Serial.print('Brick x, y: ');
          //Serial.print(x);
          //Serial.print(', ');
          //Serial.print(y);
          //Serial.print('\n');
          Serial.print("index: ");
          Serial.print(brickIndex);
          Serial.print("\n");
          _bricks[brickIndex] = NULL;
//          _bricks[brickIndex] = new Brick(_screen, x, y, brickWidth, brickHeight, color, _lineColor, _backgroundColor);
        }
      }
    }
    
    ~Gamefield() {
      for (int i = 0; i < _numBricks; i++)
        delete _bricks[i];

      delete _bricks;
    }

    void drawInitial() {
      _screen.begin();
      _screen.setRotation(0);
      
      _screen.background(_backgroundColor.r, _backgroundColor.g, _backgroundColor.b);
      _screen.stroke(_lineColor.r, _lineColor.g, _lineColor.b);

      drawPad();

//      for (int i = 0; i < _numBricks; i++)
//        _bricks[i]->draw();

      _ball.draw();
    }

    void drawPad() {
      _screen.moveRoundRect(_prevPadPos.x-_padHalfWidth, _prevPadPos.y-_padHalfHeight, 
                            _padPos.x-_padHalfWidth, _padPos.y-_padHalfHeight, 
                            _padWidth, _padHeight,
                            _padHeight / 2,
                            _padColor,
                            _lineColor,
                            _backgroundColor);      

      _prevPadPos = _padPos;
    }

    void tick() {
      readPadSpeed();

      _ball.wallCollision();

      if (_ball.isOut()) {
        _ball.reset();
        _ball.draw();

        return;
      }

//      for (int i = 0; i < _numBricks; i++) {
//        _bricks[i]->collision(_ball);
//      }

      checkPadCollision();

      _ball.move();
      _ball.draw();
      movePad(_padPos + _padSpeed);
    }

    void readPadSpeed() {
        int joyX = analogRead(_joyXPin);
        _padSpeed.x = map(joyX, 0, 1023, -5, 6);
    }

    void movePad(Vector newPos) {
      if (newPos.x - _padHalfWidth >= 0 && newPos.x + _padHalfWidth < _width)
        _padPos = newPos;

      drawPad();    
    }

    void checkPadCollision() {
      // Already flying up (may be after low collision with pad)
      if (_ball.speed.y < 0)
        return;

      // Higher than touching a pad
      if (_ball.center.y + _ball.radius < _padPos.y-_padHalfHeight)
        return;
      
      // To allow more area for side collision
      const int smallerHalfWidth = _padHalfWidth * 0.8;
      const int sideKickBallSpeed = 1;

      // Top pad section
      if (_ball.center.x >= _padPos.x - smallerHalfWidth &&
          _ball.center.x <= _padPos.x + smallerHalfWidth) {
        _ball.speed.y *= -1;
        _ball.speed.x = _ball.speed.x >> 1;
        _ball.speed.x += _padSpeed.x >> 1;
        return;
      }

      // Left side kick
      if (_ball.center.x < _padPos.x - smallerHalfWidth &&
          _ball.center.x + _ball.radius >= _padPos.x - _padHalfWidth) {

        _ball.speed.x = - sideKickBallSpeed;
        _ball.speed.y *= -1;
      }

      // Right side kick
      if (_ball.center.x > _padPos.x + smallerHalfWidth &&
          _ball.center.x - _ball.radius <= _padPos.x + _padHalfWidth) {

        _ball.speed.x = sideKickBallSpeed;
        _ball.speed.y *= -1;
      }
    }
  
  private:
    Brick **_bricks;
    Ball _ball;
    Vector _padPos;
    Vector _padSpeed;
    Vector _prevPadPos;
    int _width;
    int _height;
    int _padWidth;
    int _padHeight;
    int _padHalfWidth;
    int _padHalfHeight;
    int _numBricks;
    Color _backgroundColor;
    Color _lineColor;
    Color _padColor;
    ExtendedTFT _screen;
    int _joyXPin;
    int _joyYPin;
};


Gamefield *gf;
  
void setup() {
  Serial.begin(9600);
  while (!Serial) {
    ;  // wait for serial port to connect. Needed for native USB port only
  }

  gf = new Gamefield(
    128, // gamefield width
    160, // gamefield height
    5,   // top margin,
    5,   // left margin,
    2,   // gap,
    5,   // brick radius,
    3,   // ball radius,
    20,  // pad width
    4,   // pad height
    2,   // num rows,
    10,  // num cols,
    CS_PIN,
    DC_PIN,
    RST_PIN,
    JOY_X_PIN,
    JOY_Y_PIN);

//  gf->drawInitial();
}

void loop() {
//  gf->tick();
//  delay(10);
}
