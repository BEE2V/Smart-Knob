#include <Arduino.h>
#include <SPI.h>
#include <Adafruit_GFX.h>
#include <Adafruit_ST7789.h>

// ================= TFT =================

#define TFT_CS 10
#define TFT_DC 8
#define TFT_RST 9

#define TFT_MOSI 11
#define TFT_SCLK 12

Adafruit_ST7789 tft(TFT_CS, TFT_DC, TFT_RST);

// ================= Inputs =================

#define ENC_CLK 20
#define ENC_DT 21
#define ENC_SW 47

#define BTN1 48
#define BTN2 45
#define BTN3 0
#define BTN4 35

// =================================================
//                 MENU SYSTEM
// =================================================

struct MenuItem
{
  const char *name;

  MenuItem *parent;

  MenuItem *children;

  int childCount;

  void (*action)();
};

// Forward declarations

void emptyAction();

// -------- Actions --------

void lightOn()
{
  Serial.println("Living Room ON");
}

void lightOff()
{
  Serial.println("Living Room OFF");
}

// -------- Menu Tree --------

MenuItem livingRoomChildren[] =
    {
        {"ON", nullptr, nullptr, 0, lightOn},
        {"OFF", nullptr, nullptr, 0, lightOff}};

MenuItem lightsChildren[] =
    {
        {"Living Room", nullptr, livingRoomChildren, 2, emptyAction},
        {"Bedroom", nullptr, nullptr, 0, emptyAction},
        {"All Lights", nullptr, nullptr, 0, emptyAction}};

MenuItem climateChildren[] =
    {
        {"Temperature", nullptr, nullptr, 0, emptyAction},
        {"Fan", nullptr, nullptr, 0, emptyAction}};

MenuItem mediaChildren[] =
    {
        {"Play", nullptr, nullptr, 0, emptyAction},
        {"Volume", nullptr, nullptr, 0, emptyAction}};

MenuItem settingsChildren[] =
    {
        {"Display", nullptr, nullptr, 0, emptyAction},
        {"WiFi", nullptr, nullptr, 0, emptyAction}};

MenuItem rootChildren[] =
    {
        {"Lights", nullptr, lightsChildren, 3, emptyAction},
        {"Climate", nullptr, climateChildren, 2, emptyAction},
        {"Media", nullptr, mediaChildren, 2, emptyAction},
        {"Settings", nullptr, settingsChildren, 2, emptyAction}};

MenuItem root =
    {
        "HOME",
        nullptr,
        rootChildren,
        4,
        emptyAction};

MenuItem *currentMenu = &root;

int selected = 0;

void emptyAction()
{
  Serial.println("Action executed");
}

// =================================================
//             LINK TREE PARENTS
// =================================================

void linkParents(MenuItem *menu)
{

  for (int i = 0; i < menu->childCount; i++)
  {

    menu->children[i].parent = menu;

    if (menu->children[i].children != nullptr)
    {
      linkParents(&menu->children[i]);
    }
  }
}

// =================================================
//                 ENCODER
// =================================================

int encoderSteps = 0;

int lastEncoded = 0;

bool menuChanged = false;

void encoderTask()
{

  int MSB = digitalRead(ENC_CLK);
  int LSB = digitalRead(ENC_DT);

  int encoded = (MSB << 1) | LSB;

  int sum = (lastEncoded << 2) | encoded;

  int movement = 0;

  if (sum == 0b1101 ||
      sum == 0b0100 ||
      sum == 0b0010 ||
      sum == 0b1011)
  {
    movement = 1;
  }

  if (sum == 0b1110 ||
      sum == 0b0111 ||
      sum == 0b0001 ||
      sum == 0b1000)
  {
    movement = -1;
  }

  if (movement)
  {

    encoderSteps += movement;

    if (abs(encoderSteps) >= 2)
    {

      selected += (encoderSteps > 0) ? 1 : -1;

      encoderSteps = 0;

      if (selected < 0)
        selected = currentMenu->childCount - 1;

      if (selected >= currentMenu->childCount)
        selected = 0;

      menuChanged = true;
    }
  }

  lastEncoded = encoded;
}

// =================================================
//                 DRAW MENU
// =================================================

void drawMenu()
{

  tft.fillScreen(ST77XX_BLACK);

  tft.setTextSize(2);

  tft.setCursor(10, 10);

  tft.setTextColor(ST77XX_YELLOW);

  tft.println(currentMenu->name);

  for (int i = 0; i < currentMenu->childCount; i++)
  {

    tft.setCursor(20, 50 + i * 30);

    if (i == selected)
    {
      tft.setTextColor(ST77XX_GREEN);
      tft.print("> ");
    }
    else
    {
      tft.setTextColor(ST77XX_WHITE);
      tft.print("  ");
    }

    tft.println(currentMenu->children[i].name);
  }
}

// =================================================
//                 ENTER
// =================================================

void selectPressed()
{

  MenuItem *item = &currentMenu->children[selected];

  Serial.print("SELECT: ");
  Serial.println(item->name);

  if (item->children != nullptr)
  {

    currentMenu = item;

    selected = 0;
  }

  else
  {

    item->action();
  }

  menuChanged = true;
}

// =================================================
//                 BACK
// =================================================

void backPressed()
{

  Serial.println("BACK");

  if (currentMenu->parent != nullptr)
  {

    currentMenu = currentMenu->parent;

    selected = 0;

    menuChanged = true;
  }
}

// =================================================
//                 HOME
// =================================================

void homePressed()
{

  currentMenu = &root;

  selected = 0;

  menuChanged = true;
}

// =================================================
//              BUTTON HANDLING
// =================================================

bool lastButtons[5] = {false, false, false, false, false};

void buttonsTask()
{

  bool state[5];

  state[0] = !digitalRead(BTN1);
  state[1] = !digitalRead(BTN2);
  state[2] = !digitalRead(BTN3);
  state[3] = !digitalRead(BTN4);
  state[4] = !digitalRead(ENC_SW);

  for (int i = 0; i < 5; i++)
  {

    if (state[i] && !lastButtons[i])
    {

      if (i == 0)
        homePressed();

      if (i == 1)
      {
        selected--;
        if (selected < 0)
          selected = currentMenu->childCount - 1;

        menuChanged = true;
      }

      if (i == 2)
      {
        selected++;

        if (selected >= currentMenu->childCount)
          selected = 0;

        menuChanged = true;
      }

      if (i == 3)
        backPressed();

      if (i == 4)
        selectPressed();
    }

    lastButtons[i] = state[i];
  }
}

// =================================================
//                     SETUP
// =================================================

void setup()
{

  Serial.begin(115200);

  SPI.begin(TFT_SCLK, -1, TFT_MOSI, TFT_CS);

  tft.init(240, 320);

  tft.setRotation(0);

  tft.fillScreen(ST77XX_BLACK);

  pinMode(ENC_CLK, INPUT_PULLUP);
  pinMode(ENC_DT, INPUT_PULLUP);
  pinMode(ENC_SW, INPUT_PULLUP);

  pinMode(BTN1, INPUT_PULLUP);
  pinMode(BTN2, INPUT_PULLUP);
  pinMode(BTN3, INPUT_PULLUP);
  pinMode(BTN4, INPUT_PULLUP);

  lastEncoded =
      (digitalRead(ENC_CLK) << 1) |
      digitalRead(ENC_DT);

  linkParents(&root);

  drawMenu();
}

// =================================================
//                      LOOP
// =================================================

void loop()
{

  encoderTask();

  buttonsTask();

  if (menuChanged)
  {

    drawMenu();

    menuChanged = false;
  }
}