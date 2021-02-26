// Koala Throttle
// -----------------------------------------------------------------------------

#define BT
#ifdef BT
#include <BluetoothSerial.h>
#endif

#include <WiFi.h>

#include "brakes.h"
#include "buttons.h"
#include "encoder.h"
#include "file.h"
#include "koala.h"
#include "menu.h"
#include "pcRead.h"
#include "pins.h"
#include "pots.h"
#include "physics.h"
#include "server.h"
#include "vars.h"

unsigned int debug = 1;

// -----------------------------------------------------------------------------
// Initialize the OLED display using Wire library

#define SCREEN_WID  128
#define SCREEN_HT    64

#ifdef SH1106
SH1106Wire  display(0x3c, 21, 22);
#else
SSD1306Wire  display(0x3c, 21, 22);
#endif

#ifdef BT
BluetoothSerial     serialBT;
#endif
WiFiClient          wifi;

// -----------------------------------------------------------------------------
const char * stateStr [] = {
    "ST_WIFI",
    "ST_JMRI",
    "ST_NO_LOCO",
    "ST_HORN",
    "ST_MENU",
    "ST_CFG",
    "ST_DVT",
    "ST_ECHO",
    "ST_ESTOP",
};
#define N_STATE_STR  (sizeof(stateStr)/sizeof(char *))

char s0[30];
char s1[30];
char s2[30];
char s [80];

// ---------------------------------------------------------
// check if loco # has changes
//     update jmri with new # or
//     send dispatch/release to jmri
void chkLoco (void)
{
    static int  locoLst = 0;

    if (locoLst == loco)
        return;

    sprintf (s, "%s: loco %d, locoLst %d,", __func__, loco, locoLst);
    Serial.println (s);

    if (0 != locoLst)  {
        state |= ST_NO_LOCO;
        wifiSend ("MT-*<;>r");    // release all
        wifiSend ("TS0");
    }

    if (0 != loco)  {
        state &= ~ST_NO_LOCO;

        sprintf (s, "T%c%d", 128 < loco ? 'L' : 'S', loco);
        wifiSend (s);
    }

    locoLst = loco;
}

// ---------------------------------------------------------
// display up to 4 lines of text
void dispOled(
    const char  *s0,
    const char  *s1,
    const char  *s2,
    const char  *s3,
    bool         clr )
{
    char  s [40];

    if (clr)
        display.clear();

    display.setTextAlignment(TEXT_ALIGN_LEFT);

    if (s0)  {
        display.drawString(0, DISP_Y0,  s0);
        if (debug && NUL != *s0)  {
            sprintf (s, "... %s", s0);
            if (ST_ECHO & state)
                Serial.println (s);
        }
    }
    if (s1)  {
        display.drawString(0, DISP_Y1, s1);
        if (debug && NUL != *s1)  {
            sprintf (s, "    %s", s1);
            if (ST_ECHO & state)
                Serial.println (s);
        }
    }
    if (s2)  {
        display.drawString(0, DISP_Y2, s2);
        if (debug && NUL != *s2)  {
            sprintf (s, "    %s", s2);
            if (ST_ECHO & state)
                Serial.println (s);
        }
    }
    if (s3)  {
        display.drawString(0, DISP_Y3, s3);
        if (debug && NUL != *s3)  {
            sprintf (s, "    %s", s3);
            if (ST_ECHO & state)
                Serial.println (s);
        }
    }
    display.display();
}

// ---------------------------------------------------------
// default display screen
static void dispDefault (void)
{
    char *t = NULL;

    if (! (ST_WIFI & state))
        t = (char*) "No WiFi";
    else if (! (ST_JMRI & state))
        t = (char*) "No JMRI";
    else if (ST_NO_LOCO & state)
        t = (char*) "No LOCO";
    else  {
        sprintf (s, "%2d:%02d   %d", timeSec / 60, timeSec % 60, loco);
        sprintf (s0, "   %3d Thr  %s", throttle, brakeStr [brake]);
        sprintf (s1, "   %3d Spd  %s", mph,
                DIR_NEUTRAL == dir ? "Neutral"
                    : DIR_FOR == dir ? "Forward" : "Reverse");
    }

    if (t) {
        sprintf (s, "%2d:%02d   %s", timeSec / 60, timeSec % 60, t);
        strcpy (s0, "");
        strcpy (s1, "");
    }

#if 0
    static int timeSecLst = 0;
    if (timeSecLst != timeSec || state & ST_MENU)  {
    {
        timeSecLst = timeSec;
        dispOled (s, s0, s1, 0, CLR);
    }
#else
    dispOled (s, s0, s1, 0, CLR);
#endif
}

// ---------------------------------------------------------
// display inputs
static void dispInputs (void)
{
    byte  encA = 10 * digitalRead (Enc_A_Dt) + digitalRead (Enc_A_Clk);
    byte  encB = 10 * digitalRead (Enc_B_Dt) + digitalRead (Enc_B_Clk);

    sprintf (s,  "%02d:%02d %s", timeSec / 60, timeSec % 60, name);
#if 0
    sprintf (s0, "bkA %03d, bkB %3d", encApos, encBpos);
#else
    sprintf (s0, "encA %02d, encB %02d", encA, encB);
#endif
    sprintf (s1, "%3d, %3d, %3d, %3d",
                            throttle, reverser, whistle, slope);
    if (button)
        sprintf (s2, "key %02d", button);
    else
        sprintf (s2, "key __");

    dispOled(s, s0, s1, s2, CLR);
}

// ---------------------------------------------------------
// background display for menu
void
dispMenu (void)
{
    static int timeSecLst = 0;
    if (timeSecLst == timeSec)
        return;
    timeSecLst = timeSec;

    if (! loco)  {
        sprintf (s, "%2d:%02d   No Loco", timeSec / 60, timeSec % 60);
    }
    else  {
        sprintf (s, "%2d:%02d  Loco %d", timeSec / 60, timeSec % 60, loco);
    }

    dispOled (s, 0, 0, 0, CLR);
}

// ---------------------------------------------------------
// e-stop
void
eStop (void)
{
    static unsigned long msecEstop = 0;
           unsigned long msec      = millis();

    if (! (ST_E_STOP & state))  {
        sprintf (s, "TV%d", -1);
        wifiSend (s);
        msecEstop = msec;
        state |= ST_E_STOP;
    }

#define TIMEOUT_ESTOP    5000
    else if (msec - msecEstop > TIMEOUT_ESTOP)
        state &= ~ST_E_STOP;

    if (! ((msec / 500) % 2))
        dispOled ("E-STOP", 0, 0, 0, CLR);
    else
        dispOled (0 ,"E-STOP", 0, 0, CLR);
}

// -------------------------------------------------------------------
// connect to jmri
static void jmriConnect (void)
{
    sprintf (s0, "%d", port);
    dispOled("JMRI connecting", host, s0, 0, CLR);

#if 0
    sprintf (s, "JMRI connecting, %s, %s", host, s0, 0);
    Serial.println (s);
#endif

    if (wifi.connect(host, port))  {
        state |= ST_JMRI;

        dispOled("JMRI connected", 0, 0, 0, CLR);
        sprintf (s, "N%s", name);
        wifiSend (s);

        delay (1000);
    }

    dispOled(0, host, s0, 0, CLR);
    delay (1000);
}

// ---------------------------------------------------------
#define N_FUNC 29
int funcState [N_FUNC] = {};

int jmriFuncKey (
    unsigned func,
    int       cmd )
{
    if (N_FUNC <= func)  {
        printf ("jmriFuncKey: invalid function - %d\n", func);
        return 1;
    }

    switch (cmd)  {
    case FUNC_CLR:
        funcState [func] = 0;
        break;

    case FUNC_SET:
        funcState [func] = 1;
        break;

    case FUNC_TGL:
    default:
        funcState [func] ^= 1;
        break;
    }

    sprintf (s, "TF%d%d", funcState [func], func);
    wifiSend (s);

    return 0;
}

// ---------------------------------------------------------
// connect to wifi
static void wifiConnect (void)
{
    if (WL_CONNECTED == WiFi.begin (ssid, pass))  {
        state |= ST_WIFI;

        IPAddress ip = WiFi.localIP ();
        sprintf (s, "%u.%u.%u.%u", ip[0], ip[1], ip[2], ip[3]);

        dispOled("WiFi connected", 0, s, 0, CLR);
        serverInit ();
        delay (1000);
    }
    else  {
 //     delay (1000);
        dispOled("WiFi connecting", ssid, pass, 0, CLR);
#if 0
        sprintf ((char*)"WiFi connecting, %s, %s", ssid, pass);
        Serial.println (s);
#endif

        delay (1000);
        dispOled(0, ssid, pass, 0, CLR);
    }
}

// ---------------------------------------------------------
// display wifi responses on serial monitor
static void wifiReceive (void)
{
    static char cLst = 0;

    while (wifi.available()) {
        char c = wifi.read();
        if ('\r' == c)
            continue;

        if ('\n' == cLst && '\n' == c)
            continue;

            Serial.write (c);
        cLst = c;
    }
}

// ---------------------------------------------------------
// common routine for sending strings to wifi and flushing
void
wifiSend (
    const char*  s )
{
    Serial.print ("wifiSend: ");
    Serial.println (s);

    wifi.println (s);
    wifi.flush ();
}

// -----------------------------------------------------------------------------
void loop()
{
    static unsigned long msecLst  = 0;

    if (ST_E_STOP & state) {
        eStop ();
        return;
    }

    do {
        msec    = millis();
#define Period  100
    } while (msec - msecLst < Period);

    msecLst = msec;
    timeSec = msec / 1000;

    // some debuging
    static int stateLst = 0;
    if (debug && stateLst != state)  {
        stateLst = state;

        printf ("%s: state 0x%02x -", __func__, state);
        for (unsigned i = 0; i < N_STATE_STR; i++)
            if (state & (1<<i))
                printf (" %s", stateStr [i]);
        printf ("\n");
    }

    // -------------------------------------
    // attempt wifi connection
    if (! (ST_WIFI & state) && ! (ST_CFG & state))  {
        wifiConnect ();
    }

    // -------------------------------------
    // attempt jmri connection if WiFi established
    else if (! (ST_JMRI & state) && ! (ST_CFG & state))  {
        jmriConnect ();
    }

    // -------------------------------------
    // display default screen
    else if (! (ST_CFG & state))  {
        if (ST_MENU & state)  {
            menu (M_NULL);
        }
        else if (ST_DVT & state)
            dispInputs ();
        else
            dispDefault ();
    }

    // -------------------------------------
    // scan interfaces
    potsRead ();
    buttonsChk ();

    // -------------------------------------
    // scan external interfaces
    server ();
    wifiReceive ();

    // check serial I/Fs and update state appropriately
    int res = pcRead (Serial);
#ifdef BT
    if (! res)
        res = pcRead (serialBT);
#endif
    state = res ? state | ST_CFG : state & ~ST_CFG;

    // -------------------------------------
    // update JMRI
    chkLoco ();
    if (! (ST_NO_LOCO & state))
        physics (msec);
}

// -----------------------------------------------------------------------------
void
setup (void)
{
    Serial.begin (115200);
    Serial.print   (name);
    Serial.print   (" - ");
    Serial.println (version);

#ifdef BT
    serialBT.begin (name);
#endif

    SPIFFS.begin (true);
#if 1
    varsLoad ();
#endif

    // init hardware
    pinMode (LED, OUTPUT);

    buttonsInit ();
    encoderInit ();     // configures interrupts

    // init OLED display
    display.init();
 // display.flipScreenVertically();
    display.setFont(ArialMT_Plain_16);
    display.clear();
    display.setTextAlignment(TEXT_ALIGN_LEFT);
    display.setColor(WHITE);

    // -------------------------------------
    dispOled(name, version, 0, 0, CLR);

    state = ST_NO_LOCO;

    // if button pressed during startup, skip wifi/jmri connections
    buttonsChk ();
    if (button)
        state |= ST_WIFI | ST_JMRI;

    delay (1000);
}