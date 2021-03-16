// physics models

#define Sim
#ifdef  Sim
# include <stdio.h>
#else
# include <Arduino.h>
#endif

#include "brakes.h"
#include "engine.h"
#include "koala.h"
#include "phyConst.h"
#include "physics.h"
#include "rollRes.h"
#include "vars.h"

unsigned long msecLst = 0;
int           dMsec;

#define ABS(x)   (0 > (x) ? -(x) : (x))
#define SGN(x)   (0 > (x) ? -1   : 1)
#define NBR     0.10        // nominal brake ratio

// ---------------------------------------------------------
void _reverser (void)
{
    if (DIR_FOR != dir && (MAX_MID + 10) < reverser)  {
        dir = DIR_FOR;
        wifiSend ("TR1");
        printf (" %s: %3d For\n", __func__, reverser);
    }
    else if (DIR_REV != dir && (MAX_MID - 10) > reverser)  {
        dir = DIR_REV;
        wifiSend ("TR0");
        printf (" %s: %3d Rev\n", __func__, reverser);
    }
    else if (DIR_NEUTRAL != dir &&
        (MAX_MID - 10) < reverser && reverser < (MAX_MID + 10))  {
        dir      = DIR_NEUTRAL;
        printf (" %s: %3d Neutral\n", __func__, reverser);
    }
}

// ---------------------------------------------------------
void _throttle (void)
{
    tractEff = tractEffMax * throttle / MAX_THR;
}

// -----------------------------------------------------------------------------
#define G   32.2        // gravitation acceleration ft/sec/sec

float brkF;
float grF;              // grade
float acc;
float force;

int   cutoff;

int   a[10];
float fps = 0;          // should be initialized when loco set
int   b[10];

int   hdr = 0;
int   mphLst = 0;
float whRes;
int   wtTot;

int secLst = 0;
int sec;
int mins;

// ---------------------------------------------------------
void
disp (
    unsigned long msec,
    int           flag,
    int           brkMode )
{
    static unsigned int  cnt    = -1;
    static char constants = 0;

    if (! constants)  {
        constants++;
        printf ("%s: wtLoco %d, wtCar %d, wtTot %d, mass %d\n",
            __func__, wtLoco, wtCar, wtTot, mass);
        brakesMdlPr ();
    }

    mins  = msec / 60000;
    sec   = msec % 60000;
    cnt++;

#if 0
    if (! (sec % 1000) || ! (cnt % flag))  {
#else
    if (! (cnt % flag))  {
#endif
        if (! (hdr++ % 10))  {
            printf (" %7s", "time");
            printf (" %4s %3s %3s", "cars", "cut", "thr");
            printf (" %6s %6s %6s %6s", "drawBr", "res", "grF", "brF");
            printf (" %5s %6s %4s %4s", "force", "acc", "fps", "mph");

            if (brkMode)
                brakesPr (1);
            else
                enginePr (1);
            printf ("\n");
        }

        // time
        printf (" %2d:%04.1f", mins, sec / 1000.0);

        printf (" %4d", cars);
        printf (" %3d", cutoff);
        printf (" %s%2d", 0>dir ? "-" : " ", throttle);
        printf (" %6d", tractEff);
        printf (10 > whRes      ? " %6.2f" : " %6.0f", whRes);
        printf (" %6.1f", grF);
        printf (10 > brkF       ? " %6.2f" : " %6.0f", brkF);
        printf (10 > ABS(force) ? " %5.2f" : " %5.0f", force);

        printf (" %6.2f", acc);
        printf (10 > fps ? " %4.4f" : " %4.0f", fps);
        printf (10 > mph ? " %4.2f" : " %4.1f", mph);

        if (brkMode)
            brakesPr (0);
        else
            enginePr (0);

        printf ("\n");
    }
}

// ---------------------------------------------------------
void
physics (
    unsigned long msec,
    int           dispFlag,
    int           brkMode )
{
    if (0)
        printf ("%s: %ld msec, flag %d, brkMode %d\n",
            __func__, msec, dispFlag, brkMode);


    dMsec   = msec - msecLst;

    _reverser ();
    brakes (dMsec);
#if 0
    _throttle ();
#else
    cutoff      = 1.6 * ABS(reverser-MAX_MID);
    tractEff    = engineTe (dMsec / 1000.0, fps, throttle, cutoff);
#endif

    tonnage = (cars * wtCar);
    wtTot   = wtLoco + tonnage;

    // -------------------------------------
    // forces
    //
    // tractive effort
 // force  = dir * tractEff;
    force  = tractEff;

    // wheel resistance
    float rf   =  rollRes (fps/MphTfps);
    float tons =  wtTot / LbPton;
    whRes      = 0.1 < fps ? rf * tons : 0;     // only when rolling
 // force     -= dir * whRes;
    force     -= whRes;

    if (0) printf (" %s: rf %.2f, tons %.0f, whRes %.2f\n",
        __func__, rf, tons, whRes);

    // grade
    grF    = wtTot * grX10 / 1000;      // slope
 // force -= dir * grF;
    force -= grF;

    // brakes
    brkF  = (cars * wtCar) * NBR * brakeAirPct / 100;
    brkF +=         wtLoco * NBR * brakeIndPct / 100;

    if (0 == mph && ABS(force) < brkF)  // shouldn't force car to move
        brkF =  SGN(force) * force;

    force -= dir * brkF;

    // -------------------------------------
    // acceleration (Newton's equation)
    mass  = wtTot / G;
    acc   = force / mass;
    fps  += acc * dMsec / 1000;
    mph   = fps / MphTfps;

    if (0) printf (" %s: mass %d\n", __func__, mass);

    // display values
    disp (msec, dispFlag, brkMode);

    msecLst = msec;
    secLst  = sec;
}
