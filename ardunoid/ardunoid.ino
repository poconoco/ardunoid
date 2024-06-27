/*
 * (c) Leonid Yurchenko
 * https://www.youtube.com/@nocomake
 */

#include <TFT.h>  
#include <SPI.h>
#include <EEPROM.h>

// TFT Screen pins
#define CS_PIN   6
#define DC_PIN   7
#define RST_PIN  5

// Joystick pins
#define JOY_X_PIN 0
#define JOY_Y_PIN 1
#define JOY_X_DIR -1  // 1 or -1
#define JOY_X_TRIM -10  // Applied to raw reading which is 0..1023

#define SPEAKER_PIN 8

#define BTN_START_PIN 2
#define BTN_FIRE_PIN 12

#define SCREEN_ROTATION 0

#define MAX_BALLS 5
#define BRICK_COLS 9
#define BRICK_ROWS 8
#define BRICK_NUM (BRICK_COLS * BRICK_ROWS)

// Scaling is about geometry calculations versus rendering.
// We need to do geometry math in integers, but have decent accuracy, 
// so we need more precision in geometry than pixels, hence
// scaling of the geometry up when setting objects positions and sizes, 
// and then during rendering - back down
#define SCALE_BITS 3

// Geometry constants
#define BRICK_GAP (3 << SCALE_BITS)
#define BRICK_TOP_MARGIN (18 << SCALE_BITS)
#define BRICK_SIDE_MARGIN (15 << SCALE_BITS)
#define STATS_LINE_HEIGHT (10 << SCALE_BITS)
#define STATS_RIGHT_MARGIN (80 << SCALE_BITS)
#define STATS_BALL_GAP (2 << SCALE_BITS)
#define BULLET_LENGTH (5 << SCALE_BITS)
#define BULLET_SPEED (5 << SCALE_BITS)
#define PAD_SHRINK_PER_LEVEL (1 << SCALE_BITS)  // Shrink by 1px from each side for each level
#define MAX_PAD_SPEED (6 << SCALE_BITS)

// Non-geometry UI constants
#define BASE_FONT_WIDTH 6  // By the TFT library for font text size = 1, to use for positioning
#define BASE_FONT_HEIGHT 12  // Including distane between lines

#define TITLE_Y 100
#define SUBTITLE_Y 122

#define RGB565(r, g, b) (((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3))

#define BG_COLOR RGB565(0, 0, 0)
#define BALL_COLOR RGB565(255, 255, 0)
#define PAD_COLOR RGB565(80, 80, 255)
#define STROKE_COLOR RGB565(255, 255, 255)
#define BULLET_COLOR RGB565(255, 100, 50)

const int BRICK_COLORS[] = {
  RGB565(0, 70, 255),
  RGB565(255, 255, 0),
  RGB565(255, 130, 0),
  RGB565(0, 180, 0)
};

int textWidth(const char *text, uint8_t size) {
  return strlen(text) * BASE_FONT_WIDTH * size;
}

struct Vector {
  Vector() {}
  
  Vector(const int _x, const int _y) 
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
      pinMode(BTN_START_PIN, INPUT_PULLUP);
      pinMode(BTN_FIRE_PIN, INPUT_PULLUP);

      _screen.begin();
      _screen.initR(INITR_BLACKTAB);  // If your screen has BGR color order (instead of RGB)
      _screen.setRotation(SCREEN_ROTATION);

      _realWidth = _screen.width();

      _width = _realWidth << SCALE_BITS;
      _height = _screen.height() << SCALE_BITS;
      
      _brickHalfWidth = ((_width - (BRICK_SIDE_MARGIN * 2) - (BRICK_GAP * (BRICK_COLS - 1)) ) / BRICK_COLS) / 2;
      _brickHalfHeight = (_brickHalfWidth + (1 << SCALE_BITS)) / 2;
      _brickHalfHeight = (_brickHalfHeight >> SCALE_BITS) << SCALE_BITS;  // Round

      _ballRadius = ((_brickHalfWidth * 25 / 30) >> SCALE_BITS) << SCALE_BITS;
      _bricks = new Brick[BRICK_NUM];
      
      for (uint8_t row = 0; row < BRICK_ROWS; row++) {
        for (uint8_t col = 0; col < BRICK_COLS; col++) {
          uint8_t brickIndex = row * BRICK_COLS + col;
          _bricks[brickIndex].center.x = BRICK_SIDE_MARGIN + (col * (_brickHalfWidth * 2 + BRICK_GAP)) + _brickHalfWidth;
          _bricks[brickIndex].center.y = STATS_LINE_HEIGHT + BRICK_TOP_MARGIN + (row * (_brickHalfHeight * 2 + BRICK_GAP)) + _brickHalfHeight;
        }
      }
    }
    
    ~Gamefield() {
      delete _bricks;
    }

    void resetGame() {
      _level = 1;
      _score = 0;
      _balls = MAX_BALLS;

      // Uncomment to reset highscore
      // EEPROM.put(0, _score);
    }

    void startLevel() {
      _poppedBricks = 0;
      _bulletFired = false;
      _padSpeed.x = 0;
      _padSpeed.y = 0;

      // Pad gets shrinked when progressing through levels
      _padHalfWidth = max(_brickHalfWidth * 3 / 2, (_brickHalfHeight * 8) - (_level - 1) * PAD_SHRINK_PER_LEVEL);

      _screen.background(BG_COLOR);
      _screen.stroke(STROKE_COLOR);
      _screen.setTextSize(1);

      resetBall();

      _padPos.y = _height - _brickHalfHeight - 2;
      movePad(_width / 2, true);

      _screen.drawLine(0, STATS_LINE_HEIGHT >> SCALE_BITS, _realWidth, STATS_LINE_HEIGHT >> SCALE_BITS, STROKE_COLOR);
      _screen.text("LVL", (_width - STATS_RIGHT_MARGIN) >> SCALE_BITS, 2);
      drawStatsBalls();
      drawStatsLevel();
      drawStatsScore();

      for (uint8_t i = 0; i < BRICK_NUM; i++) {
        _bricks[i].popped = false;
        const int x = (_bricks[i].center.x - _brickHalfWidth) >> SCALE_BITS;
        const int y = (_bricks[i].center.y - _brickHalfHeight) >> SCALE_BITS;
        const int w = _brickHalfWidth >> (SCALE_BITS - 1);
        const int h = _brickHalfHeight >> (SCALE_BITS - 1);
        _screen.drawRect(x, y, w, h, STROKE_COLOR);
        _screen.fillRect(x + 1, y + 1, w - 2, h - 2, BRICK_COLORS[(i / BRICK_COLS) % 4]);
      }

      const char title[9] = "LEVEL XX";
      itoa(_level, title + 6, 10);

      drawLargeTitle(title);
      drawSubtitle("press start");
      waitForStartBtn();
      clearTitles();
    }

    void tick() {
      if (_balls < 0) {
        // Game over
        return;
      }

      Vector potentialBallPos(_ballPos + _ballSpeed);

      // Calculate left and right walls collisions first
      if ((potentialBallPos.x + _ballRadius >= _width) || (potentialBallPos.x - _ballRadius <= 0)) {
        _ballSpeed.x = -_ballSpeed.x;
        tone(SPEAKER_PIN, 300, 10);
      }

      // Then top wall collision
      if (potentialBallPos.y - _ballRadius <= STATS_LINE_HEIGHT + (1 << SCALE_BITS)) {
        _ballSpeed.y = -_ballSpeed.y;
        tone(SPEAKER_PIN, 300, 10);
      }
  
      // Then brick collisions
      bool ballPoppedBrick = false;
      for (int i = 0; i < BRICK_NUM; i++) {
        if (! ballPoppedBrick && checkBrickBallCollision(_bricks[i], potentialBallPos)) {
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

      // Check if level is completed
      if (_poppedBricks == BRICK_NUM) {
        nextLevel();
        return;
      }

      readPadSpeed();

      moveBall(_ballPos.x + _ballSpeed.x, _ballPos.y + _ballSpeed.y);
      // Force pad redraw if ball is low enough to overlap with the pad 
      movePad(_padPos.x + _padSpeed.x, _ballPos.y + _ballRadius + (3 << SCALE_BITS) >= _padPos.y - _brickHalfHeight);
      moveBullet();

      checkPause();
    }

  protected:
    void waitForStartBtn() {
      // Wait till button pressed
      while (digitalRead(BTN_START_PIN) == HIGH) {};
      delay(50);

      // Wait till button will be released
      while (digitalRead(BTN_START_PIN) == LOW) {};
      delay(50);
    }

    void checkPause() {
      // If not pressed, do nothing
      if (digitalRead(BTN_START_PIN) == HIGH)
        return;

      // Wait till button will be released
      while (digitalRead(BTN_START_PIN) == LOW) {};
      delay(50);

      // User engaged pause
      drawLargeTitle("PAUSE");
      drawSubtitle("press start");

      waitForStartBtn();
      clearTitles();
    }

    void drawBullet(int color) {
        _screen.drawLine(_bulletPos.x >> SCALE_BITS, _bulletPos.y >> SCALE_BITS,
                         _bulletPos.x >> SCALE_BITS, (_bulletPos.y + BULLET_LENGTH) >> SCALE_BITS, color);
    }

    void moveBullet() {
      if (_bulletFired) {
        // Clear prev
        drawBullet(BG_COLOR);

        _bulletPos.y -= BULLET_SPEED;
        
        if (_bulletPos.y <= STATS_LINE_HEIGHT + BULLET_LENGTH)
          _bulletFired = false;
        else
          drawBullet(BULLET_COLOR);  // Draw new
      } else if (digitalRead(BTN_FIRE_PIN) == LOW) {
        // Firing Bullet
        _bulletFired = true;
        _bulletPos.x = _padPos.x;
        _bulletPos.y = _padPos.y - _brickHalfHeight - BULLET_LENGTH - (1 << SCALE_BITS);
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
      const int x = ((_width - STATS_RIGHT_MARGIN) >> SCALE_BITS) + (BASE_FONT_WIDTH * 3);
      const char buff[3];
      itoa(_level, buff, 10);

      _screen.fillRect(x, 2, BASE_FONT_WIDTH * 2, 7, BG_COLOR);
      _screen.text(buff, x, 2);
    }

    void drawStatsScore() {
      const char buff[6];
      itoa(_score, buff, 10);

      const int textW = textWidth(buff, 1);
      const int x = _realWidth - textW;
      _screen.fillRect(x, 2, textW, 7, BG_COLOR);
      _screen.text(buff, x, 2);
    }

    void drawBall(int x, int y, int color) {
        _screen.fillCircle(x >> SCALE_BITS, y >> SCALE_BITS, _ballRadius >> SCALE_BITS, color);
    }

    void drawStatsBalls() {
      for (uint8_t i = 0; i < MAX_BALLS; i++)
        drawBall(STATS_BALL_GAP + _ballRadius + i * (_ballRadius * 2 + STATS_BALL_GAP),
                 STATS_LINE_HEIGHT / 2,
                 i < _balls ? BALL_COLOR : BG_COLOR);  // clear or draw
    }

    void readPadSpeed() {
        int joyX = analogRead(JOY_X_PIN) + JOY_X_TRIM;

        // Map to symmetric range around zero
        joyX = map(joyX, 0, 1023, -MAX_PAD_SPEED, MAX_PAD_SPEED) * JOY_X_DIR;
        
        // Add some inertia by mixing prev and new speed value as 90%/10%
        // (this ratio combined with framerate controls how much inertia there is)
        _padSpeed.x = (_padSpeed.x * 9 + joyX * 1) / 10;
    }

    void moveBall(int newX, int newY) {
      // Clean prev but only when there will be no new
      int oldX = _ballPos.x >> SCALE_BITS;
      int oldY = _ballPos.y >> SCALE_BITS;
      int r = _ballRadius >> SCALE_BITS;
      int rSquare = r * r;
      int dx = (newX >> SCALE_BITS) - oldX;
      int dy = (newY >> SCALE_BITS) - oldY;
      // Clean prev circle, except new
      for (int y = -r; y <= r; y++) {
        for (int x = -r; x <= r; x++) {
          if (x*x + y*y > rSquare + 1)
            continue;  // Outside of old circle

          if ((x-dx)*(x-dx) + (y-dy)*(y-dy) <= rSquare)
            continue;  // Inside new circle

          _screen.drawPixel(oldX + x, oldY + y, BG_COLOR);
        }
      }

      // Draw new
      drawBall(newX, newY, BALL_COLOR);

      _ballPos.x = newX;
      _ballPos.y = newY;
    }

    void movePad(int newX, bool forceRedraw) {
      if (newX < _padHalfWidth) {
        newX = _padHalfWidth;
        _padSpeed.x = 0;
      }
        
      if (newX + _padHalfWidth >= _width) {
        newX = _width - _padHalfWidth;
        _padSpeed.x = 0;
      }

      if (! forceRedraw && newX == _padPos.x)
        return;

      int x = (_padPos.x - _padHalfWidth) >> SCALE_BITS;
      int y = (_padPos.y - _brickHalfHeight) >> SCALE_BITS;
      int w = _padHalfWidth >> (SCALE_BITS - 1);
      int h = _brickHalfHeight >> (SCALE_BITS - 1);
      int dx = (abs(_padPos.x - newX) + _brickHalfHeight) >> SCALE_BITS;

      // Clear old
      if (newX > _padPos.x) 
        _screen.fillRect(x, y, dx, h, BG_COLOR);
      else
        _screen.fillRect(x + w - dx, y, dx, h, BG_COLOR);

      // Update X
      _padPos.x = newX;
      x = (_padPos.x - _padHalfWidth) >> SCALE_BITS;

      // Draw new
      _screen.fillRect(x + 1, y + 1, w - 2, h - 2, PAD_COLOR);
      _screen.drawRoundRect(x, y, w, h,
                            _brickHalfHeight >> SCALE_BITS,
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

    bool checkBrickBallCollision(Brick &brick, const Vector &ballPos) {
      if (brick.popped)
        return false;

      int distX = abs(ballPos.x - brick.center.x) - _brickHalfWidth - _ballRadius;
      int distY = abs(ballPos.y - brick.center.y) - _brickHalfHeight - _ballRadius;

      if (distX > 0 || distY > 0) 
        return false;
          
      // Collision
      popBrick(brick);

      // Determine collision side
      if (distX > -_ballRadius && distY > -_ballRadius) {
        // If both X and Y penetration is less than radius, then it's a corner hit
        
        if ((ballPos.x > brick.center.x) == (_ballSpeed.x < 0))
          _ballSpeed.x = -_ballSpeed.x;

        if ((ballPos.y > brick.center.y) == (_ballSpeed.y < 0))
          _ballSpeed.y = -_ballSpeed.y;
      } else if (distX > distY) {
        // Left or right collision
        _ballSpeed.x = -_ballSpeed.x;
      } else 
      {
        // Top or Bottom collision
        _ballSpeed.y = -_ballSpeed.y;
      }

      return true;
    }

    bool checkPadCollision(const Vector &ballPos) {
      // Already flying up (may be after low collision with pad)
      if (_ballSpeed.y < 0)
        return false;

      // Higher than touching a pad
      if (ballPos.y + _ballRadius < _padPos.y - _brickHalfHeight)
        return false;
      
      // Too low to pick up
      if (ballPos.y > _padPos.y)
        return false;

      // To allow more area for side collision
      const int smallerHalfWidth = _padHalfWidth - (3 << SCALE_BITS);

      // Top pad section
      if (ballPos.x >= _padPos.x - smallerHalfWidth &&
          ballPos.x <= _padPos.x + smallerHalfWidth) {

        // Bounce up, absolute y speed never changes
        _ballSpeed.y = -_ballSpeed.y;

        // If x speed is significant, slow down
        if (abs(_ballSpeed.x) >> SCALE_BITS > 1)
          _ballSpeed.x = _ballSpeed.x / 2;

        // Transfer half of the X speed difference to the ball
        _ballSpeed.x += (_padSpeed.x - _ballSpeed.x) / 2;

        return true;
      }

      // Left side kick
      if (ballPos.x < _padPos.x - smallerHalfWidth &&
          ballPos.x + _ballRadius >= _padPos.x - _padHalfWidth) {

        _ballSpeed.x = -1 << SCALE_BITS;
        _ballSpeed.y = - _ballSpeed.y;

        return true;
      }

      // Right side kick
      if (ballPos.x > _padPos.x + smallerHalfWidth &&
          ballPos.x - _ballRadius <= _padPos.x + _padHalfWidth) {

        _ballSpeed.x = 1 << SCALE_BITS;
        _ballSpeed.y = - _ballSpeed.y;

        return true;
      }

      return false;
    }

    void popBrick(Brick &brick) {
      // Draw background
      _screen.fillRect((brick.center.x - _brickHalfWidth) >> SCALE_BITS, 
                       (brick.center.y - _brickHalfHeight) >> SCALE_BITS, 
                       _brickHalfWidth >> (SCALE_BITS - 1), 
                       _brickHalfHeight >> (SCALE_BITS - 1), BG_COLOR);

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

      _balls--;

      if (_balls < 0) {
        gameOver();
        return;
      }

      resetBall();
      drawStatsBalls();
    }

    void resetBall() {
      _ballSpeed = Vector((1 << SCALE_BITS) / 2, -3 << (SCALE_BITS - 1));

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

    void drawSubtitle(const char* subtitle, int line = 0) {
      _screen.text(subtitle, (_realWidth - textWidth(subtitle, 1)) / 2, SUBTITLE_Y + line * BASE_FONT_HEIGHT);
    }

    void clearTitles() {
      // Draw over the level label
      _screen.fillRect(0, TITLE_Y, _realWidth, 40, BG_COLOR);
    }

    void gameOver() {
      uint16_t highscore;
      EEPROM.get(0, highscore);
      if (_score > highscore) {
        highscore = _score;
        EEPROM.put(0, highscore);
      }


      drawLargeTitle("GAME OVER");
      const char scoreBuff[] = "SCORE XXXXX";
      const char highscoreBuff[] = "HIGH SCORE XXXXX";
      utoa(_score, scoreBuff+6, 10);
      utoa(highscore, highscoreBuff+11, 10);

      drawSubtitle(scoreBuff);
      drawSubtitle(highscoreBuff, 1);

      tone(SPEAKER_PIN, 120, 500);
      delay(500);

      waitForStartBtn();
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
    int _width;
    int _height;
    int _realWidth;  // In real pixels, without scaling up with SCALE_BITS
    int _brickHalfWidth;
    int _brickHalfHeight;
    int _ballRadius;
    int _padHalfWidth;
    TFT _screen;
    uint8_t _level;
    uint16_t _score;
    int8_t _balls;
    uint8_t _poppedBricks;
};

void setup() {
  Gamefield gamefield;
  gamefield.resetGame();
  gamefield.startLevel();

  unsigned long prevTick = 0;
  unsigned long currTick = 0;
  while (true) {
    gamefield.tick();
    currTick = millis();

    // Make tick rate consistent even if tick takes different time to calculate
    if (currTick - prevTick < 20)
      delay(20 - (currTick - prevTick));

    prevTick = currTick;
  }
}
