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

#define SCREEN_ROTATION 0

#define BRICK_COLS 9
#define BRICK_ROWS 8
#define BRICK_NUM (BRICK_COLS * BRICK_ROWS)
#define BRICK_GAP 2
#define BRICK_TOP_MARGIN 15
#define BRICK_SIDE_MARGIN 15
#define STATUS_LINE_HEIGHT 10
#define MAX_BALLS 3
#define BASE_FONT_WIDTH 6  // By the TFT library for font text size = 1, to use for positioning
#define STATS_RIGHT_MARGIN 100
#define TITLES_Y 100

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
  Vector center;
  bool popped;
};

class Gamefield {
  public:
    Gamefield()
      : _screen(CS_PIN, DC_PIN, RST_PIN)
    {
      _screen.begin();
      _screen.initR(INITR_BLACKTAB);
      _screen.setRotation(SCREEN_ROTATION);

      _bgColor = _screen.newColor(0, 0, 0);
      _strokeColor = _screen.newColor(255, 255, 255);
      _ballColor = _screen.newColor(255, 255, 0);
      _padColor = _screen.newColor(0, 0, 255);

      _width = _screen.width();
      _height = _screen.height();
      
      _brickWidth = (_width - (BRICK_SIDE_MARGIN*2) - (BRICK_GAP*(BRICK_COLS - 1)) ) / BRICK_COLS;
      _brickHeight = _brickWidth / 2 + 1;
      _ballRadius = _brickWidth / 3;

      _padHalfWidth = _brickHeight * 3;
      _padHalfHeight = 2;
      
      _level = 1;
      _score = 0;
      _balls = MAX_BALLS;

      _bricks = new Brick[BRICK_NUM];
      
      for (uint8_t row = 0; row < BRICK_ROWS; row++) {
        for (uint8_t col = 0; col < BRICK_COLS; col++) {
          uint8_t brickIndex = row * BRICK_COLS + col;
          _bricks[brickIndex].center.x = BRICK_SIDE_MARGIN + (col * (_brickWidth + BRICK_GAP)) + _brickWidth / 2;
          _bricks[brickIndex].center.y = STATUS_LINE_HEIGHT + BRICK_TOP_MARGIN + (row * (_brickHeight + BRICK_GAP)) + _brickHeight / 2;
        }
      }
    }
    
    ~Gamefield() {
      delete _bricks;
    }

    void startLevel() {
      _ballPos.x = _width/2;
      _ballPos.y = _height*2/3;

      _padPos.x = _width/2;
      _padPos.y = _height - _padHalfHeight * 2.4;
      _prevPadPos = _padPos;
      _ballSpeed.x = 1;
      _ballSpeed.y = -2;
      _poppedBricks = 0;

      _screen.background(_bgColor);
      _screen.stroke(_strokeColor);
      _screen.setTextSize(1);

      _screen.line(0, STATUS_LINE_HEIGHT, _width, STATUS_LINE_HEIGHT);

      drawPad();
      drawStatsBalls();
      drawStatsLabels();
      drawStatsLevel();
      drawStatsScore();

      uint16_t brickColors[] = {
        _screen.newColor(0, 100, 255),
        _screen.newColor(255, 100, 0),
        _screen.newColor(255, 255, 0),
        _screen.newColor(255, 0, 100),
        _screen.newColor(0, 255, 0)
      };

      for (uint8_t i = 0; i < BRICK_NUM; i++) {
        Brick &brick = _bricks[i];
        brick.popped = false;
        const uint8_t x = brick.center.x - _brickWidth / 2;
        const uint8_t y = brick.center.y - _brickHeight / 2;
        _screen.fillRect(x, y, 
                         _brickWidth, _brickHeight,
                         _strokeColor);
        _screen.fillRect(x + 1, y + 1, 
                         _brickWidth - 2, _brickHeight - 2,
                         brickColors[(i / BRICK_COLS) % 5]);
      }
    }

    void nextLevel() {
      _level++;
      startLevel();
      const char title[] = "LEVEL XX";
      if (_level <= 99)
        itoa(_level, title + 6, 10);

      drawLargeTitle(title);
      delay(1000);

      // Draw over the level label
      const uint8_t textWidth = strlen(title) * TITLES_Y * 2;
      _screen.fillRect((_width - textWidth) / 2, TITLES_Y, textWidth, 20, _bgColor);
    }

    void drawStatsLabels() {
      const uint8_t x = _width - STATS_RIGHT_MARGIN;
      _screen.text("LVL", x, 2);
    }

    void drawStatsLevel() {
      const uint8_t x = _width - STATS_RIGHT_MARGIN + (BASE_FONT_WIDTH * 3);
      const char buff[3] = "XX";
      if (_level <= 99)
        itoa(_level, buff, 10);

      _screen.fillRect(x, 2, BASE_FONT_WIDTH * 2, 7, _bgColor);
      _screen.text(buff, x, 2);
    }

    void drawStatsScore() {
      const char buff[10] = "XXXXXXXXX";
      if (_score <= 999999999)
        itoa(_score, buff, 10);

      const uint8_t numDigits = strlen(buff);
      const uint8_t x = _width - (BASE_FONT_WIDTH * numDigits);
      _screen.fillRect(x, 2, BASE_FONT_WIDTH * numDigits, 7, _bgColor);
      _screen.text(buff, x, 2);
    }

    void drawStatsBalls() {
      for (uint8_t i = 0; i < MAX_BALLS; i++)
        _screen.fillCircle(_ballRadius + 2 + i * (_ballRadius * 2 + 2), 
                           STATUS_LINE_HEIGHT / 2, 
                           _ballRadius, 
                           i < _balls ? _ballColor : _bgColor);  // clear or draw
    }

    void drawPad() {
      if (_prevPadPos.x != _padPos.x || _prevPadPos.y != _padPos.y)
        // Clear old
        _screen.fillRect(_prevPadPos.x - _padHalfWidth, _prevPadPos.y - _padHalfHeight, 
                         _padHalfWidth * 2, _padHalfHeight * 2,
                         _bgColor);

      // Draw new
      _screen.fillRect(_padPos.x - _padHalfWidth+1, _padPos.y - _padHalfHeight + 1, 
                       _padHalfWidth * 2 - 2, _padHalfHeight * 2 - 2, 
                       _padColor);
      _screen.drawRoundRect(_padPos.x-_padHalfWidth, _padPos.y-_padHalfHeight, 
                            _padHalfWidth * 2, _padHalfHeight * 2, 
                            _padHalfHeight, _strokeColor);

      _prevPadPos = _padPos;
    }

    void tick() {
      if (_balls < 0) {
        // Game over
        return;
      }

      Vector potentialBallPos = _ballPos + _ballSpeed;
      readPadSpeed();

      // Calculate walls collisions first
      if ((potentialBallPos.x + _ballRadius >= _width) || (potentialBallPos.x - _ballRadius <= 0)) {
        _ballSpeed.x = -_ballSpeed.x;
        tone(SPEAKER_PIN, 300, 10);
      }

      if (potentialBallPos.y - _ballRadius <= STATUS_LINE_HEIGHT) {
        _ballSpeed.y = -_ballSpeed.y;
        tone(SPEAKER_PIN, 300, 10);
      }

      // Then brick collisions
      for (int i = 0; i < BRICK_NUM; i++) {
        if (checkBrickCollision(_bricks[i], potentialBallPos)) {
          tone(SPEAKER_PIN, 500, 10);
          break; // Collide with one brick at a time
        }
      }

      // Then pad collision
      if (checkPadCollision())
        tone(SPEAKER_PIN, 150, 50);

      // Check ball flew out of the bottom
      if (potentialBallPos.y - _ballRadius >= _height) {
        ballOut();
        return;
      }

      if (_poppedBricks == BRICK_NUM) {
        nextLevel();
        return;
      }

      moveBall(_ballPos + _ballSpeed);
      movePad(_padPos + _padSpeed);
    }

    void readPadSpeed() {
        int joyX = analogRead(JOY_X_PIN);
        _padSpeed.x = map(joyX, 0, 1023, -5 * JOY_X_DIR, 6 * JOY_X_DIR);
    }

    void moveBall(Vector newPos) {
      // Clear prev
      _screen.fillRect(_ballPos.x - _ballRadius, _ballPos.y - _ballRadius, _ballRadius * 2 + 1, _ballRadius * 2 + 1, _bgColor);

      // Draw new
      _screen.fillCircle(newPos.x, newPos.y, _ballRadius, _ballColor);

      _ballPos = newPos;
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
          _ballSpeed.x = min(relBallX / 3, 2);
          if (abs(relBallY) > _brickHeight)
            _ballSpeed.y = -_ballSpeed.y;
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
      _screen.fillRect(brick.center.x - _brickWidth / 2, 
                       brick.center.y - _brickHeight / 2, 
                       _brickWidth, _brickHeight, _bgColor);
      // Mark popped
      brick.popped = true;
      _score += 10;
      drawStatsScore();
      _poppedBricks++;
    }

    void ballOut() {
      tone(SPEAKER_PIN, 300, 200);
      delay(200);
      tone(SPEAKER_PIN, 200, 200);
      delay(200);

      _balls -= 1;

      if (_balls < 0) {
        gameOver();
        return;
      }

      _ballSpeed = Vector(0, -2);
      moveBall(Vector(_width/2, _height*2/3));
      drawStatsBalls();
    }

    void drawLargeTitle(const char *title) {
      const uint8_t textWidth = strlen(title) * BASE_FONT_WIDTH * 2;

      _screen.setTextSize(2);
      _screen.text(title, (_width-textWidth)/2, TITLES_Y);
      _screen.setTextSize(1);
    }

    void gameOver() {
      drawLargeTitle("GAME OVER");
      const char scoreBuff[10] = "XXXXXXXXX";
      if (_score <= 999999999)
        itoa(_score, scoreBuff, 10);

      _screen.text("SCORE", (_width-(BASE_FONT_WIDTH * 5))/2, TITLES_Y + 22);
      _screen.text(scoreBuff, (_width-(BASE_FONT_WIDTH * strlen(scoreBuff)))/2, TITLES_Y + 22 + 10);
    }
  
  private:
    Brick *_bricks;
    Vector _ballPos;
    Vector _ballSpeed;
    Vector _padPos;
    Vector _padSpeed;
    Vector _prevPadPos;
    uint8_t _width;
    uint8_t _height;
    uint8_t _brickWidth;
    uint8_t _brickHeight;
    uint8_t _ballRadius;
    uint8_t _padHalfWidth;
    uint8_t _padHalfHeight;
    uint16_t _bgColor;
    uint16_t _strokeColor;
    uint16_t _ballColor;
    uint16_t _padColor;
    TFT _screen;
    uint16_t _level;
    uint32_t _score;
    int8_t _balls;
    int8_t _poppedBricks;
};


Gamefield *gamefield;

void setup() {
  gamefield = new Gamefield();
  gamefield->startLevel();
}

void loop() {
  gamefield->tick();
  delay(10);
}
