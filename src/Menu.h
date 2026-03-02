#ifndef Menu_h
#define Menu_h
#include "Arduino.h"
#include <OneButton.h>
#include <TFT_eSPI.h>

typedef struct __attribute__((packed)) {
    char ref[11];
    float qrg;
    uint8_t modmsg;
} loraSpot;

class Menu {
public:
    Menu();
    void init();
    static void tickButtons();
    static void draw();
    
private:

    static OneButton _btn_left;
    static OneButton _btn_right;
    static OneButton _btn_up;
    static OneButton _btn_down;
    static OneButton _btn_click;
    static uint8_t _line;
    static String bands[];
    static float qrg[];
    static String modes[];
    static String messages[];
    static uint8_t qrgidx;
    static uint8_t modeidx;
    static uint8_t msgidx;

    static void _b_click();
    static void _b_up();
    static void _b_down();
    static void _b_left();
    static void _b_right();
    static uint8_t _setHigh(uint8_t l);

};
#endif
