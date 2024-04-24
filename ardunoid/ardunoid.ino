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
#define JOY_X_TRIM -3;

#define SPEAKER_PIN 8

#define BTN_A_PIN 2
#define BTN_B_PIN 3

#define SCREEN_ROTATION 0

#define MAX_BALLS 3
#define BRICK_COLS 9
#define BRICK_ROWS 8
#define BRICK_NUM (BRICK_COLS * BRICK_ROWS)

// Below macro constants are for scaling geometry calculations versus rendering.
// We need it to do geometry math in integers, but have decent accuracy, so use
#define SCALE 8
#define SCALE_BITS 3

// Geometry constants
#define BRICK_GAP (2 << SCALE_BITS)
#define BRICK_TOP_MARGIN (15 << SCALE_BITS)
#define BRICK_SIDE_MARGIN (15 << SCALE_BITS)
#define STATUS_LINE_HEIGHT (10 << SCALE_BITS)
#define STATS_RIGHT_MARGIN (100 << SCALE_BITS)
#define STATS_BALL_GAP (2 << SCALE_BITS)
#define BULLET_LENGTH (5 << SCALE_BITS)
#define BULLET_SPEED (5 << SCALE_BITS)
#define PAD_SHRINK_PER_LEVEL (2 << SCALE_BITS)

// Non-geometry UI constants
#define BASE_FONT_WIDTH 6  // By the TFT library for font text size = 1, to use for positioning
#define TITLE_Y 100
#define SUBTITLE_Y 122

#define RGB565(r, g, b) ((r << 11) | (g << 5) | b)

#define BG_COLOR RGB565(0, 0, 0)
#define BALL_COLOR RGB565(255, 255, 0)
#define PAD_COLOR RGB565(0, 0, 255)
#define STROKE_COLOR RGB565(255, 255, 255)

static const uint16_t BRICK_COLORS[] = {
  RGB565(0, 100, 255),
  RGB565(180, 180, 0),
  RGB565(255, 0, 255),
  RGB565(0, 180, 0)
};

uint16_t textWidth(const char *text, uint8_t size) {
  return strlen(text) * BASE_FONT_WIDTH * size;
}

struct Vector {
  public:
    Vector() {}
    
    Vector(const int16_t _x, const int16_t _y) 
      : x(_x)
      , y(_y)
    {}

    Vector operator + (const Vector &operand) {
      return Vector(x + operand.x, y + operand.y);
    }

    void operator += (const Vector &operand) {
      x = x + operand.x;
      y = y + operand.y;
    }

    int16_t x;
    int16_t y;
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
      pinMode(BTN_A_PIN, INPUT_PULLUP);
      pinMode(BTN_B_PIN, INPUT_PULLUP);

      _screen.begin();
      _screen.initR(INITR_BLACKTAB);
      _screen.setRotation(SCREEN_ROTATION);

      _realWidth = _screen.width();
      _width = _realWidth << SCALE_BITS;
      _height = _screen.height() << SCALE_BITS;
      
      _brickWidth = (_width - (BRICK_SIDE_MARGIN*2) - (BRICK_GAP*(BRICK_COLS - 1)) ) / BRICK_COLS;
      _brickHalfWidth = _brickWidth / 2;

      _brickHeight = _brickHalfWidth + 1;
      _brickHalfHeight = _brickHeight / 2;

      _ballRadius = _brickWidth / 3;
      _bricks = new Brick[BRICK_NUM];
      
      for (uint8_t row = 0; row < BRICK_ROWS; row++) {
        for (uint8_t col = 0; col < BRICK_COLS; col++) {
          uint8_t brickIndex = row * BRICK_COLS + col;
          _bricks[brickIndex].center.x = BRICK_SIDE_MARGIN + (col * (_brickWidth + BRICK_GAP)) + _brickHalfWidth;
          _bricks[brickIndex].center.y = STATUS_LINE_HEIGHT + BRICK_TOP_MARGIN + (row * (_brickHeight + BRICK_GAP)) + _brickHalfHeight;
        }
      }

      resetGame();
    }
    
    ~Gamefield() {
      delete _bricks;
    }

    void startLevel() {
      _poppedBricks = 0;
      _bulletFired = false; // No bullet
      _padSpeed.x = 0;
      _padSpeed.y = 0;

      // Resetting pad size here, because pad gets shrinked when progressing through levels
      _padHalfWidth = _brickHeight * 3;
      _padHalfWidth = max(_padHalfWidth / 4, _padHalfWidth - (_level - 1) * PAD_SHRINK_PER_LEVEL);
      _padHalfHeight = _brickHalfHeight;

      _screen.background(BG_COLOR);
      _screen.stroke(STROKE_COLOR);
      _screen.setTextSize(1);

      resetBall();

      _padPos.y = _height - _padHalfHeight * 2.4;
      movePad(_width / 2, true);

      _screen.fillRect(0, STATUS_LINE_HEIGHT >> SCALE_BITS, _realWidth, 1, STROKE_COLOR);

      _screen.text("LVL", (_width - STATS_RIGHT_MARGIN) >> SCALE_BITS, 2);
      drawStatsBalls();
      drawStatsLevel();
      drawStatsScore();

      for (uint8_t i = 0; i < BRICK_NUM; i++) {
        Brick &brick = _bricks[i];
        brick.popped = false;
        const uint16_t x = (brick.center.x - _brickHalfWidth) >> SCALE_BITS;
        const uint16_t y = (brick.center.y - _brickHalfHeight) >> SCALE_BITS;
        const uint8_t w = _brickWidth >> SCALE_BITS;
        const uint8_t h = _brickHeight >> SCALE_BITS;
        _screen.fillRect(x, y, w, h, STROKE_COLOR);
        _screen.fillRect(x + 1, y + 1, w - 2, h - 2, BRICK_COLORS[(i / BRICK_COLS) % 4]);
      }

      const char title[] = "LEVEL XX";
      if (_level <= 99)
        itoa(_level, title + 6, 10);

      drawLargeTitle(title);
      drawSubtitle("press A to start");
      waitForBtnA();

      clearTitles();
    }

    void waitForBtnA() {
      // Wait till button pressed
      while (digitalRead(BTN_A_PIN) == HIGH) {};
      delay(50);

      // Wait till button will be released
      while (digitalRead(BTN_A_PIN) == LOW) {};
      delay(50);
    }

    void checkPause() {
      // If not pressed, do nothing
      if (digitalRead(BTN_A_PIN) == HIGH)
        return;

      // Wait till button will be released
      while (digitalRead(BTN_A_PIN) == LOW) {};
      delay(50);

      // User engaged pause
      delay(50);
      drawLargeTitle("PAUSE");
      drawSubtitle("press A");

      waitForBtnA();
      clearTitles();
    }

    void drawBullet(uint16_t color) {
        _screen.fillRect(_bulletPos.x >> SCALE_BITS, _bulletPos.y >> SCALE_BITS, 1, BULLET_LENGTH >> SCALE_BITS, color);
    }

    void moveBullet() {
      if (_bulletFired) {
        // Clear prev
        drawBullet(BG_COLOR);

        _bulletPos.y -= BULLET_SPEED;
        
        if (_bulletPos.y <= STATUS_LINE_HEIGHT)
          _bulletFired = false;
        else
          drawBullet(STROKE_COLOR);  // Draw new
      } else {
        if (digitalRead(BTN_B_PIN) == HIGH)
          return;  // Not pressed

        // Firing Bullet
        _bulletFired = true;
        _bulletPos.x = _padPos.x;
        _bulletPos.y = _padPos.y - _padHalfHeight - BULLET_LENGTH;
      }
    }

    void nextLevel() {
      tone(SPEAKER_PIN, 500, 200);
      delay(200);
      tone(SPEAKER_PIN, 600, 200);
      delay(200);

      _level++;

      startLevel();
    }

    void drawStatsLevel() {
      const uint16_t x = ((_width - STATS_RIGHT_MARGIN) >> SCALE_BITS) + (BASE_FONT_WIDTH * 3);
      const char buff[3] = "XX";
      if (_level <= 99)
        itoa(_level, buff, 10);

      _screen.fillRect(x, 2, BASE_FONT_WIDTH * 2, 7, BG_COLOR);
      _screen.text(buff, x, 2);
    }

    void drawStatsScore() {
      const char buff[8] = "XXXXXXX";
      if (_score <= 9999999)
        itoa(_score, buff, 10);

      const uint8_t textW = textWidth(buff, 1);
      const uint16_t x = _realWidth - textW;
      _screen.fillRect(x, 2, textW, 7, BG_COLOR);
      _screen.text(buff, x, 2);
    }

    void drawBall(uint16_t x, uint16_t y, uint16_t color) {
        _screen.fillCircle(x >> SCALE_BITS, y >> SCALE_BITS, _ballRadius >> SCALE_BITS, color);
    }

    void drawStatsBalls() {
      for (uint8_t i = 0; i < MAX_BALLS; i++)
        drawBall(_ballRadius + i * (_ballRadius * 2 + STATS_BALL_GAP) + STATS_BALL_GAP,
                 STATUS_LINE_HEIGHT / 2,
                 i < _balls ? BALL_COLOR : BG_COLOR);  // clear or draw
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

      if (potentialBallPos.y - _ballRadius <= STATUS_LINE_HEIGHT + (1 << SCALE_BITS)) {
        _ballSpeed.y = -_ballSpeed.y;
        tone(SPEAKER_PIN, 300, 10);
      }

      // Then brick collisions
      bool ballPoppedBrick = false;
      for (int i = 0; i < BRICK_NUM; i++) {
        if (! ballPoppedBrick && checkBrickCollision(_bricks[i], potentialBallPos)) {
          tone(SPEAKER_PIN, 500, 10);
          ballPoppedBrick = true; // Collide with one brick at a time
        }

        // Bullet collisions
        if (checkBrickBulletCollision(_bricks[i]))
        {
          tone(SPEAKER_PIN, 500, 20);

          // Clear bullet
          drawBullet(BG_COLOR);
          _bulletFired = false;
        }
      }

      // Then pad collision
      if (checkPadCollision(potentialBallPos))
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

      moveBall(_ballPos.x + _ballSpeed.x, _ballPos.y + _ballSpeed.y);
      movePad(_padPos.x + _padSpeed.x, false);
      moveBullet();

      checkPause();
    }

    void readPadSpeed() {
        int joyX0 = analogRead(JOY_X_PIN);

        // Map to more friendly way to apply power
        int joyX = map(joyX0, 0, 1023, -100, 100) + JOY_X_TRIM;

        // Make square curve (-10000..10000)
        if (joyX > 0)
          joyX = joyX * joyX * JOY_X_DIR;
        else
          joyX = - joyX * joyX * JOY_X_DIR;


        _padSpeed.x = map(joyX, -10000, 10000, (-4 << SCALE_BITS), (4 << SCALE_BITS));
    }

    void moveBall(uint16_t newX, uint16_t newY) {
      // Clear prev
      _screen.fillRect((_ballPos.x - _ballRadius) >> SCALE_BITS,
                       (_ballPos.y - _ballRadius) >> SCALE_BITS,
                       ((_ballRadius * 2) >> SCALE_BITS) + 1, ((_ballRadius * 2) >> SCALE_BITS) + 1, BG_COLOR);

      // Draw new
      drawBall(newX, newY, BALL_COLOR);

      _ballPos.x = newX;
      _ballPos.y = newY;
    }

    void movePad(uint16_t newX, bool forceRedraw) {
      if (newX < _padHalfWidth)
        newX = _padHalfWidth;
        
      if (newX + _padHalfWidth >= _width)
        newX = _width - _padHalfWidth - (1 << SCALE_BITS);

      if (! forceRedraw && newX == _padPos.x)
        return;

      uint16_t x = (_padPos.x - _padHalfWidth) >> SCALE_BITS;
      uint16_t y = (_padPos.y - _padHalfHeight) >> SCALE_BITS;
      uint16_t w = (_padHalfWidth * 2) >> SCALE_BITS;
      uint16_t h = (_padHalfHeight * 2) >> SCALE_BITS;

      // Clear old
      _screen.fillRect(x, y, w, h, BG_COLOR);

      // Update X
      _padPos.x = newX;
      x = (_padPos.x - _padHalfWidth) >> SCALE_BITS;

      // Draw new
      _screen.fillRect(x + 1, y + 1, w - 2, h - 2, PAD_COLOR);
      _screen.drawRoundRect(x, y, w, h,
                            _padHalfHeight >> SCALE_BITS,
                            STROKE_COLOR);
    }

    bool checkBrickBulletCollision(Brick &brick) {
      if (! _bulletFired || brick.popped)
        return false;

      // Not close enough
      if (_bulletPos.y > brick.center.y + _brickHalfHeight)
        return false;

      // Missed
      if (_bulletPos.x > brick.center.x + _brickHalfWidth || _bulletPos.x < brick.center.x - _brickHalfWidth)
        return false;

      popBrick(brick);
      return true;
    }

    bool checkBrickCollision(Brick &brick, const Vector &ballPos) {
      if (brick.popped)
        return false;
      
      if (abs(brick.center.x - ballPos.x) <= _brickHalfWidth + _ballRadius &&
          abs(brick.center.y - ballPos.y) <= _brickHalfHeight + _ballRadius) {
            
        // Collision
        popBrick(brick);

        // Ball coordinates relative to brick center
        int relBallX = ballPos.x - brick.center.x + _brickHalfWidth;
        int relBallY = ballPos.y - brick.center.y + _brickHalfHeight;

        // Determine collision side
        if (abs(relBallX) > abs(relBallY)) {
          // Left or right collision
          _ballSpeed.x = relBallX > 0 ? 1 : -1;
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

    bool checkPadCollision(const Vector &ballPos) {
      // Already flying up (may be after low collision with pad)
      if (_ballSpeed.y < 0)
        return false;

      // Higher than touching a pad
      if (ballPos.y + _ballRadius < _padPos.y - _padHalfHeight)
        return false;
      
      // Too low to pick up
      if (ballPos.y > _padPos.y)
        return false;

      // To allow more area for side collision
      const int16_t smallerHalfWidth = _padHalfWidth - (3 << SCALE_BITS);

      // Top pad section
      if (ballPos.x >= _padPos.x - smallerHalfWidth &&
          ballPos.x <= _padPos.x + smallerHalfWidth) {
        _ballSpeed.y = -_ballSpeed.y;
        _ballSpeed.x = _ballSpeed.x >> 1;
        _ballSpeed.x += _padSpeed.x >> 1;

        return true;
      }

      // Left side kick
      if (ballPos.x < _padPos.x-smallerHalfWidth &&
          ballPos.x + _ballRadius >= _padPos.x - _padHalfWidth) {

        _ballSpeed.x = (-1 << SCALE_BITS);
        _ballSpeed.y = - _ballSpeed.y;

        return true;
      }

      // Right side kick
      if (ballPos.x > _padPos.x+smallerHalfWidth &&
          ballPos.x - _ballRadius <= _padPos.x + _padHalfWidth) {

        _ballSpeed.x = (1 << SCALE_BITS);
        _ballSpeed.y = - _ballSpeed.y;

        return true;
      }
    }

    void popBrick(Brick &brick) {
      // Draw background
      _screen.fillRect((brick.center.x - _brickHalfWidth) >> SCALE_BITS, 
                       (brick.center.y - _brickHalfHeight) >> SCALE_BITS, 
                       _brickWidth >> SCALE_BITS, _brickHeight >> SCALE_BITS, BG_COLOR);

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

      resetBall();
      drawStatsBalls();
    }

    void resetGame() {
      _level = 1;
      _score = 0;
      _balls = MAX_BALLS;
    }

    void resetBall() {
      _ballSpeed = Vector((1 << SCALE_BITS) / 2, -1 << SCALE_BITS);

      // Clear prev
      drawBall(_ballPos.x, _ballPos.y, BG_COLOR);
      _ballPos.x = _width / 2;
      _ballPos.y = _height * 2 / 3;
    }

    void drawLargeTitle(const char *title) {
      _screen.setTextSize(2);
      _screen.text(title, (_realWidth - textWidth(title, 2)) / 2, TITLE_Y);
      _screen.setTextSize(1);
    }

    void drawSubtitle(const char* subtitle) {
      _screen.text(subtitle, (_realWidth - textWidth(subtitle, 1)) / 2, SUBTITLE_Y);
    }

    void clearTitles() {
      // Draw over the level label
      _screen.fillRect(0, TITLE_Y, _realWidth, 40, BG_COLOR);
    }

    void gameOver() {
      drawLargeTitle("GAME OVER");
      const char scoreBuff[] = "SCORE XXXXX";
      itoa(_score, scoreBuff+6, 10);

      drawSubtitle(scoreBuff);

      tone(SPEAKER_PIN, 120, 500);
      delay(500);

      waitForBtnA();
      resetGame();
      startLevel();
    }
  
  private:
    Brick *_bricks;
    Vector _ballPos;
    Vector _ballSpeed;
    Vector _padPos;
    Vector _padSpeed;
    Vector _bulletPos;
    bool _bulletFired;
    int16_t _width;
    int16_t _height;
    int16_t _realWidth;  // In real pixels, without scaling up with SCALE_BITS
    int16_t _brickWidth;
    int16_t _brickHeight;
    int16_t _brickHalfWidth;
    int16_t _brickHalfHeight;
    int16_t _ballRadius;
    int16_t _padHalfWidth;
    int16_t _padHalfHeight;
    TFT _screen;
    uint8_t _level;
    uint16_t _score;
    int8_t _balls;
    uint8_t _poppedBricks;
};

void setup() {
  Gamefield gamefield;
  gamefield.startLevel();

  unsigned long prevTick = 0;
  unsigned long currTick = 0;
  while (true) {
    currTick = millis();
    gamefield.tick();

    // Make tick rate consistent even if tick takes different time to calculate
    if (currTick - prevTick < 20)
      delay(20 - (currTick - prevTick));

    prevTick = currTick;
  }
}
