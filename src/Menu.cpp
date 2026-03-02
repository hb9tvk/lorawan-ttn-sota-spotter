#include "Arduino.h"
#include "Menu.h"

extern TFT_eSPI tft;
extern String mySummit;
extern float dist;
extern loraSpot spot;
extern bool sendLora(uint8_t *msg, int len);

OneButton Menu::_btn_left;
OneButton Menu::_btn_right;
OneButton Menu::_btn_up;
OneButton Menu::_btn_down;
OneButton Menu::_btn_click;
uint8_t Menu::_line;

String Menu::bands[]={"80m", "60m", "40m", "30m", "20m", "17m", "15m", "12m", "10m", "6m"};
float Menu::qrg[]={3562.0,5354.0, 7032.0,10118.0,14062.0,18079.0, 21062.0, 24902.0, 28062.0, 50062.0};
String Menu::modes[]={"CW", "SSB", "FM", "DATA"};
String Menu::messages[]={"QRV now", "QRT", "TEST"};
uint8_t Menu::qrgidx=2;
uint8_t Menu::modeidx=0;
uint8_t Menu::msgidx=0;

void Menu::tickButtons() {
    _btn_click.tick();
    _btn_down.tick();
    _btn_left.tick();
    _btn_up.tick();
    _btn_right.tick();
}

void Menu::_b_click() {
    Serial.println("click");
    strncpy(spot.ref, mySummit.c_str(), sizeof(spot.ref) - 1);
    spot.ref[sizeof(spot.ref) - 1] = '\0';
    spot.qrg=qrg[qrgidx];
    spot.modmsg=modeidx + (msgidx << 4);
    bool ok = sendLora((uint8_t *)&spot, sizeof(spot));
    bool isTest = (messages[msgidx] == "TEST");

    tft.fillScreen(ok ? TFT_GREEN : TFT_RED);
    tft.setTextColor(TFT_WHITE);
    tft.setTextSize(2);
    tft.setCursor(10, 15);
    tft.println(ok ? (isTest ? "Test sent!" : "Spot sent!") : "Send");
    tft.setCursor(10, 45);
    tft.println(ok ? mySummit : "failed!");
    delay(3000);
    draw();
}

void Menu::_b_up() {
  Serial.println("up");
  if (_line==0) {
    _line=4;
  } else _line--;
  draw();    
}

void Menu::_b_down() {
  Serial.println("down");
  _line=(_line + 1) % 5;
  draw();
}

void Menu::_b_left() {
    Serial.println("left");
    switch(_line) {
        case 0:
            break;
        case 1:
            if (qrgidx==0) break;
            qrgidx--;
            draw();
            break;
        case 2:
            qrg[qrgidx]-=0.5;
            draw();
            break;
        case 3:
            if (modeidx==0) break;
            modeidx--;
            draw();
            break;        
        case 4:
            if (msgidx==0) break;
            msgidx--;
            draw();
            break;
    }  
}

void Menu::_b_right() {
    Serial.println("right"); 
    switch(_line) {
        case 0:
            break;
        case 1:
            if (qrgidx==((sizeof(qrg)/sizeof(qrg[0]))-1)) break;
            qrgidx++;
            draw();
            break;
        case 2:
            qrg[qrgidx]+=0.5;
            draw();
            break;
        case 3:
            if (modeidx==((sizeof(modes)/sizeof(modes[0]))-1)) break;
            modeidx++;
            draw();
            break;        
        case 4:
            if (msgidx==((sizeof(messages)/sizeof(messages[0]))-1)) break;
            msgidx++;
            draw();
            break;
    }    
}

void Menu::draw() {
    
    uint8_t cursor=0;
    tft.fillScreen(TFT_BLACK);
    tft.setCursor(0,cursor);
    cursor+=_setHigh(0);
    tft.println(mySummit);
    uint8_t lineY = cursor;
    cursor += _setHigh(1);
    char distbuf[8];
    snprintf(distbuf, sizeof(distbuf), "%dm", (int)dist);
    tft.setCursor(0, lineY);
    tft.print(bands[qrgidx]);
    tft.setCursor(tft.width() - tft.textWidth(distbuf), lineY);
    tft.print(distbuf);
    tft.setCursor(0,cursor);
    cursor+=_setHigh(2);
    tft.println(qrg[qrgidx]);
    tft.setCursor(0,cursor);
    cursor+=_setHigh(3);
    tft.println(modes[modeidx]);
    tft.setCursor(0,cursor);
    _setHigh(4);
    tft.println(messages[msgidx]);
}

uint8_t Menu::_setHigh(uint8_t l) {
  if (l == _line) {
    tft.setTextSize(2);   
    tft.setTextColor(TFT_WHITE,TFT_RED);
    return(20);
  } else {
    tft.setTextSize(1);   
    tft.setTextColor(TFT_WHITE,TFT_BLACK);
    return(14);
  }
}

void Menu::init() {
    Serial.println("Initializing Buttons");
    _btn_click=OneButton(0, true, true);
    _btn_down=OneButton(6, true, true);
    _btn_left=OneButton(7, true, true);
    _btn_up=OneButton(4, true, true);
    _btn_right=OneButton(5, true, true);
    _line=0;

    _btn_click.attachClick(_b_click);
    _btn_up.attachClick(_b_up);
    _btn_down.attachClick(_b_down);
    _btn_left.attachClick(_b_left);
    _btn_right.attachClick(_b_right);

    _btn_up.setClickTicks(50);
    _btn_down.setClickTicks(50);
    _btn_left.setClickTicks(50);
    _btn_right.setClickTicks(50);
}

Menu::Menu() {

}

