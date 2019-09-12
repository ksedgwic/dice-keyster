// Copyright 2019 Bonsai Software, Inc.  All Rights Reserved.

#include <bip39.h>
#include <GxEPD2_BW.h>
#include <Keypad.h>

#include <Fonts/FreeSansBold9pt7b.h>
#include <Fonts/FreeSansBold12pt7b.h>
#include <Fonts/FreeSansBold18pt7b.h>
#include <Fonts/FreeMonoBold9pt7b.h>
#include <Fonts/FreeMonoBold12pt7b.h>

extern "C" {
#include <slip39.h>
#include <slip39_wordlist.h>
}

#define ESP32 1

#if defined(SAMD51)
extern "C" {
#include "trngFunctions.h"
}
#endif

// Display
GxEPD2_BW<GxEPD2_154, GxEPD2_154::HEIGHT>
    g_display(GxEPD2_154(/*CS=*/   21,
                         /*DC=*/   17,
                         /*RST=*/  16,
                         /*BUSY=*/ 4));

// Keypad
const byte rows_ = 4;
const byte cols_ = 4;
char keys_[rows_][cols_] = {
                         {'1','2','3','A'},
                         {'4','5','6','B'},
                         {'7','8','9','C'},
                         {'*','0','#','D'}
};
byte rowPins_[rows_] = {13, 12, 27, 33};
byte colPins_[cols_] = {15, 32, 14, 22};
Keypad g_keypad = Keypad(makeKeymap(keys_), rowPins_, colPins_, rows_, cols_);

enum UIState {
    SEEDLESS_MENU,
    GENERATE_SEED,
    RESTORE_BIP39,
    RESTORE_SLIP39,
    ENTER_SHARE,
    SEEDY_MENU,
    DISPLAY_BIP39,
    CONFIG_SLIP39,
    DISPLAY_SLIP39,
};

// This will be 256 when we support multiple levels.
#define MAX_SLIP39_SHARES	16

UIState g_uistate;
String g_rolls;
bool g_submitted;
uint8_t g_master_secret[16];
Bip39 g_bip39;
int g_slip39_thresh;
int g_slip39_nshares;
char** g_slip39_shares = NULL;
char* g_restore_shares[MAX_SLIP39_SHARES];
int g_num_restore_shares = 0;
int g_selected_share;

int g_wordndx[20] = { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0,
                      0, 0, 0, 0, 0, 0, 0, 0, 0, 0, };

int g_ndx = 0;		// index of "selected" word
int g_pos = 0;		// char position of cursor
int g_scroll = 0;	// index of scrolled window
    
void setup() {
    pinMode(25, OUTPUT);	// Blue LED
    digitalWrite(25, HIGH);
    
    pinMode(26, OUTPUT);	// Green LED
    digitalWrite(26, LOW);
    
    g_display.init(115200);
    g_display.setRotation(1);

    Serial.begin(115200);
    while (!Serial);	// wait for serial to come online

    Serial.println("seedtool starting");

#if defined(SAMD51)
    trngInit();
#endif

    reset_state();
}

void loop() {
    full_window_clear();
    
    switch (g_uistate) {
    case SEEDLESS_MENU:
        seedless_menu();
        break;
    case GENERATE_SEED:
        generate_seed();
        break;
    case RESTORE_BIP39:
        Serial.println("loop: RESTORE_BIP39 unimplemented");
        break;
    case RESTORE_SLIP39:
        restore_slip39();
        break;
    case ENTER_SHARE:
        enter_share();
        break;
    case SEEDY_MENU:
        seedy_menu();
        break;
    case DISPLAY_BIP39:
        display_bip39();
        break;
    case CONFIG_SLIP39:
        config_slip39();
        break;
    case DISPLAY_SLIP39:
        display_slip39();
        break;
    default:
        Serial.println("loop: unknown g_uistate " + String(g_uistate));
        break;
    }
}

void full_window_clear() {
    g_display.firstPage();
    do
    {
        g_display.setFullWindow();
        g_display.fillScreen(GxEPD_WHITE);
    }
    while (g_display.nextPage());
}

void reset_state() {
    digitalWrite(26, LOW);  // turn off the green LED
            
    g_uistate = SEEDLESS_MENU;
    g_rolls = "";
    g_submitted = false;

    // Clear the master secret.
    memset(g_master_secret, '\0', sizeof(g_master_secret));

    // Clear the BIP39 mnemonic (regenerate on all zero secret)
    g_bip39.setPayloadBytes(sizeof(g_master_secret));
    g_bip39.setPayload(sizeof(g_master_secret), (uint8_t *) g_master_secret);

    free_slip39_shares();
    
    // Clear the restore shares
    free_restore_shares();
}

int const Y_MAX = 200;

// FreeSansBold9pt7b
int const H_FSB9 = 16;	// height
int const YM_FSB9 = 6;	// y-margin

// FreeSansBold12pt7b
int const H_FSB12 = 20;	// height
int const YM_FSB12 = 9;	// y-margin

// FreeMonoBold12pt7b
int const W_FMB12 = 14;	// width
int const H_FMB12 = 18;	// height
int const YM_FMB12 = 4;	// y-margin

void seedless_menu() {
    int xoff = 16;
    int yoff = 10;
    
    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        // g_display.fillScreen(GxEPD_WHITE);
        g_display.setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB12 + YM_FSB12);
        g_display.setFont(&FreeSansBold12pt7b);
        g_display.setCursor(xx, yy);
        g_display.println("No Seed");

        yy = yoff + 3*(H_FSB9 + YM_FSB9);
        g_display.setFont(&FreeSansBold9pt7b);
        g_display.setCursor(xx, yy);
        g_display.println("A - Generate Seed");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display.setCursor(xx, yy);
        g_display.println("B - Restore BIP39");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display.setCursor(xx, yy);
        g_display.println("C - Restore SLIP39");
    }
    while (g_display.nextPage());

    while (true) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("seedless_menu saw " + String(key));
        switch (key) {
        case 'A':
            g_uistate = GENERATE_SEED;
            return;
        case 'C':
            g_uistate = RESTORE_SLIP39;
            return;
        case NO_KEY:
        default:
            break;
        }
    }
}

void generate_seed() {
    while (true) {
        int xoff = 14;
        int yoff = 8;
    
        g_display.firstPage();
        do
        {
            g_display.setPartialWindow(0, 0, 200, 200);
            // g_display.fillScreen(GxEPD_WHITE);
            g_display.setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB12 + YM_FSB12);
            g_display.setFont(&FreeSansBold12pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("Generate Seed");

            yy += 10;
        
            yy += H_FSB9 + YM_FSB9;
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("Enter Dice Rolls");

            yy += 10;
        
            yy += H_FMB12 + YM_FMB12;
            g_display.setFont(&FreeMonoBold12pt7b);
            g_display.setCursor(xx, yy);
            g_display.printf("Rolls: %d\n", g_rolls.length());
            yy += H_FMB12 + YM_FMB12;
            g_display.setCursor(xx, yy);
            g_display.printf(" Bits: %0.1f\n", g_rolls.length() * 2.5850);

            // bottom-relative position
            xx = xoff + 10;
            yy = Y_MAX - 2*(H_FSB9 + YM_FSB9);
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("Press * to clear");
            yy += H_FSB9 + YM_FSB9;
            g_display.setCursor(xx, yy);
            g_display.println("Press # to submit");
        }
        while (g_display.nextPage());
    
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("generate_seed saw " + String(key));
        switch (key) {
        case '1': case '2': case '3':
        case '4': case '5': case '6':
            g_rolls += key;
            break;
        case '*':
            g_rolls = "";
            break;
        case '#':
            g_submitted = true;
            seed_from_rolls();
            digitalWrite(26, HIGH);		// turn on green LED
            g_uistate = DISPLAY_BIP39;
            return;
        default:
            break;
        }
    }
}

void seed_from_rolls() {
    // Convert supplied entropy into master secret.
    Sha256Class sha256;
    sha256.init();
    for(uint8_t ii=0; ii < g_rolls.length(); ii++) {
        sha256.write(g_rolls[ii]);
    }
    memcpy(g_master_secret, sha256.result(), sizeof(g_master_secret));

    // Generate the BIP39 mnemonic for this secret.
    g_bip39.setPayloadBytes(sizeof(g_master_secret));
    g_bip39.setPayload(sizeof(g_master_secret), (uint8_t *)g_master_secret);
}

void seedy_menu() {
    int xoff = 16;
    int yoff = 10;
    
    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        // g_display.fillScreen(GxEPD_WHITE);
        g_display.setTextColor(GxEPD_BLACK);

        int xx = xoff;
        int yy = yoff + (H_FSB12 + YM_FSB12);
        g_display.setFont(&FreeSansBold12pt7b);
        g_display.setCursor(xx, yy);
        g_display.println("Seed Present");

        yy = yoff + 3*(H_FSB9 + YM_FSB9);
        g_display.setFont(&FreeSansBold9pt7b);
        g_display.setCursor(xx, yy);
        g_display.println("A - Display BIP39");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display.setCursor(xx, yy);
        g_display.println("B - Generate SLIP39");
        yy += H_FSB9 + 2*YM_FSB9;
        g_display.setCursor(xx, yy);
        g_display.println("C - Wipe Seed");
    }
    while (g_display.nextPage());

    while (true) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("seedy_menu saw " + String(key));
        switch (key) {
        case 'A':
            g_uistate = DISPLAY_BIP39;
            return;
        case 'B':
            g_uistate = CONFIG_SLIP39;
            return;
        case 'C':
            reset_state();
            g_uistate = SEEDLESS_MENU;
            return;
        case NO_KEY:
        default:
            break;
        }
    }
}

void display_bip39() {
    int const nwords = 12;
    int scroll = 0;
    
    while (true) {
        int const xoff = 12;
        int const yoff = 0;
        int const nrows = 6;
    
        g_display.firstPage();
        do
        {
            g_display.setPartialWindow(0, 0, 200, 200);
            // g_display.fillScreen(GxEPD_WHITE);
            g_display.setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("BIP39 Mnemonic");
            yy += H_FSB9 + YM_FSB9;
            
            yy += 4;
        
            g_display.setFont(&FreeMonoBold12pt7b);
            for (int rr = 0; rr < nrows; ++rr) {
                int wndx = scroll + rr;
                uint16_t word = g_bip39.getWord(wndx);
                g_display.setCursor(xx, yy);
                g_display.printf("%2d %s", wndx+1, g_bip39.getMnemonic(word));
                yy += H_FMB12 + YM_FMB12;
            }
            
            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("1,7-Up,Down #-Done");
        }
        while (g_display.nextPage());
        
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("display_bip39 saw " + String(key));
        switch (key) {
        case '1':
            if (scroll > 0)
                scroll -= 1;
            break;
        case '7':
            if (scroll < (nwords - nrows))
                scroll += 1;
            break;
        case '#':
            g_uistate = SEEDY_MENU;
            return;
        default:
            break;
        }
    }
}

// Append a character to a SLIP39 config value, check range.
String config_slip39_addkey(String str0, char key) {
    String newstr;
    if (str0 == " ")
        newstr = key;
    else
        newstr = str0 + key;
    int val = newstr.toInt();
    if (val == 0)	// didn't convert to integer somehow
        return str0;
    if (val < 1)	// too small
        return str0;
    if (val > 16)	// too big
        return str0;
    return newstr;
}

void config_slip39() {
    bool thresh_done = false;
    String threshstr = "3";
    String nsharestr = "5";
    
    while (true) {
        int xoff = 20;
        int yoff = 8;
    
        g_display.firstPage();
        do
        {
            g_display.setPartialWindow(0, 0, 200, 200);
            // g_display.fillScreen(GxEPD_WHITE);
            g_display.setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("Configure SLIP39");

            yy += 10;

            yy += H_FMB12 + 2*YM_FMB12;
            g_display.setFont(&FreeMonoBold12pt7b);
            g_display.setCursor(xx, yy);
            g_display.printf(" Thresh: %s", threshstr.c_str());

            if (!thresh_done) {
                int xxx = xx + (9 * W_FMB12);
                int yyy = yy - H_FMB12;
                g_display.fillRect(xxx,
                                   yyy,
                                   W_FMB12 * threshstr.length(),
                                   H_FMB12 + YM_FMB12,
                                   GxEPD_BLACK);
                g_display.setTextColor(GxEPD_WHITE);
                g_display.setCursor(xxx, yy);
                g_display.printf("%s", threshstr.c_str());
                g_display.setTextColor(GxEPD_BLACK);
            }
            
            yy += H_FMB12 + 2*YM_FMB12;
            g_display.setCursor(xx, yy);
            g_display.printf("NShares: %s", nsharestr.c_str());

            if (thresh_done) {
                int xxx = xx + (9 * W_FMB12);
                int yyy = yy - H_FMB12;
                g_display.fillRect(xxx,
                                   yyy,
                                   W_FMB12 * nsharestr.length(),
                                   H_FMB12 + YM_FMB12,
                                   GxEPD_BLACK);
                g_display.setTextColor(GxEPD_WHITE);
                g_display.setCursor(xxx, yy);
                g_display.printf("%s", nsharestr.c_str());
                g_display.setTextColor(GxEPD_BLACK);
            }

            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);

            if (!thresh_done) {
                g_display.println("*-Clear    #-Next");
            } else {
                // If nsharestr field is empty its a prev
                if (nsharestr == " ")
                    g_display.println("*-Prev     #-Done");
                else
                    g_display.println("*-Clear    #-Done");
            }
        }
        while (g_display.nextPage());
            
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("config_slip39 saw " + String(key));
        switch (key) {
        case '0': case '1': case '2': case '3': case '4':
        case '5': case '6': case '7': case '8': case '9':
            if (!thresh_done)
                threshstr = config_slip39_addkey(threshstr, key);
            else
                nsharestr = config_slip39_addkey(nsharestr, key);
            break;
        case '*':
            if (!thresh_done) {
                threshstr = " ";
            } else {
                // If nsharestr field is empty its a prev
                if (nsharestr == " ")
                    thresh_done = false;
                else
                    nsharestr = " ";
            }
            break;
        case '#':
            if (!thresh_done) {
                thresh_done = true;
                break;
            } else {
                if (threshstr.toInt() > nsharestr.toInt()) {
                    // Threshold is greater than nshares, put the cursor
                    // back on the threshold.
                    thresh_done = false;
                    break;
                }
                g_slip39_thresh = threshstr.toInt();
                g_slip39_nshares = nsharestr.toInt();
                generate_slip39_shares();
                g_uistate = DISPLAY_SLIP39;
                return;
            }
        default:
            break;
        }
    }
}

void free_slip39_shares() {
    if (g_slip39_shares) {
        for (int ndx = 0; g_slip39_shares[ndx]; ++ndx)
            free(g_slip39_shares[ndx]);
        free(g_slip39_shares);
        g_slip39_shares = NULL;
    }
}

void generate_slip39_shares() {
    int nshares = g_slip39_nshares;

    // If there are already shares, free them.
    free_slip39_shares();
    
    // Allocate space for new shares
    g_slip39_shares = (char **) malloc(sizeof(char *) * (nshares+1));
    for (int ndx = 0; ndx < nshares; ++ndx)
        g_slip39_shares[ndx] = (char *)malloc(MNEMONIC_LIST_LEN);
    g_slip39_shares[nshares] = NULL; // sentinel

    generate_mnemonics(g_slip39_thresh, g_slip39_nshares,
                       g_master_secret, sizeof(g_master_secret),
                       NULL, 0, 0, g_slip39_shares);
}

// Extract the indicted word from the wordlist.
String slip39_select(char* wordlist, int wordndx) {
    // Start at the begining of the wordlist.
    char* pos = wordlist;

    // Advance to the nth word.
    for (int ii = 0; ii < wordndx; ++ii) {
        do {
            ++pos;
        } while (*pos != ' ');
        // stops on the next space
        ++pos; // advance to the next word.
    }

    char* pos0 = pos;
    do {
        ++pos;
    } while (*pos != ' ');
    char* epos = pos;

    return String(pos0).substring(0, epos-pos0);	// yuk
}

void display_slip39() {
    int const nwords = 20;
    int sharendx = 0;
    int scroll = 0;
    
    while (true) {
        int xoff = 12;
        int yoff = 0;
        int nrows = 6;
    
        g_display.firstPage();
        do
        {
            g_display.setPartialWindow(0, 0, 200, 200);
            // g_display.fillScreen(GxEPD_WHITE);
            g_display.setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.printf("SLIP39 %d/%d", sharendx+1, g_slip39_nshares);
            yy += H_FSB9 + YM_FSB9;
            
            yy += 4;
        
            g_display.setFont(&FreeMonoBold12pt7b);
            for (int rr = 0; rr < nrows; ++rr) {
                int wndx = scroll + rr;
                String word = slip39_select(g_slip39_shares[sharendx], wndx);
                g_display.setCursor(xx, yy);
                g_display.printf("%2d %s", wndx+1, word.c_str());
                yy += H_FMB12 + YM_FMB12;
            }
            
            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            if (sharendx < (g_slip39_nshares-1))
                g_display.println("1,7-Up,Down #-Next");
            else
                g_display.println("1,7-Up,Down #-Done");
        }
        while (g_display.nextPage());
        
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("display_slip39 saw " + String(key));
        switch (key) {
        case '1':
            if (scroll > 0)
                scroll -= 1;
            break;
        case '7':
            if (scroll < (nwords - nrows))
                scroll += 1;
            break;
        case '*':	// prev
            if (sharendx > 0) {
                --sharendx;
                scroll = 0;
            }
            break;
        case '#':	// next / done
            if (sharendx < (g_slip39_nshares-1)) {
                ++sharendx;
                scroll = 0;
            } else {
                g_uistate = SEEDY_MENU;
                return;
            }
            break;
        default:
            break;
        }
    }
}

void free_restore_shares() {
    for (int ndx = 0; ndx < g_num_restore_shares; ++ndx) {
        free(g_restore_shares[ndx]);
        g_restore_shares[ndx] = NULL;
    }
    g_num_restore_shares = 0;
}

void restore_slip39() {
    int scroll = 0;
    int selected = g_num_restore_shares;	// selects "add" initially
    
    while (true) {
        int const xoff = 12;
        int const yoff = 0;
        int const nrows = 5;
    
        // Adjust the scroll to center the selection.
        if (selected < 3)
            scroll = 0;
        else
            scroll = selected - 3;
        
        // Are we showing the restore action?
        int showrestore = g_num_restore_shares > 0;

        g_display.firstPage();
        do
        {
            g_display.setPartialWindow(0, 0, 200, 200);
            // g_display.fillScreen(GxEPD_WHITE);
            g_display.setTextColor(GxEPD_BLACK);

            int xx = xoff;
            int yy = yoff + (H_FSB9 + YM_FSB9);
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("Enter SLIP39 Shares");
            yy += H_FSB9 + YM_FSB9;

            xx = xoff + 20;
            yy += 16;

            // Do we need to scroll?
            int disprows = g_num_restore_shares + 1 + showrestore > nrows
                ? nrows
                : g_num_restore_shares + 1 + showrestore;
            
            g_display.setFont(&FreeMonoBold12pt7b);
            for (int rr = 0; rr < disprows; ++rr) {
                int sharendx = scroll + rr;
                char buffer[32];
                if (sharendx < g_num_restore_shares) {
                    sprintf(buffer, "Share %d", sharendx+1);
                } else if (sharendx == g_num_restore_shares) {
                    sprintf(buffer, "Add Share");
                } else {
                    sprintf(buffer, "Restore");
                }
                
                g_display.setCursor(xx, yy);
                if (sharendx != selected) {
                    g_display.println(buffer);
                } else {
                    g_display.fillRect(xx,
                                       yy - H_FMB12,
                                       W_FMB12 * strlen(buffer),
                                       H_FMB12 + YM_FMB12,
                                       GxEPD_BLACK);
                    g_display.setTextColor(GxEPD_WHITE);
                    g_display.println(buffer);
                    g_display.setTextColor(GxEPD_BLACK);
                }

                yy += H_FMB12 + YM_FMB12;
            }
            
            // bottom-relative position
            xx = xoff + 2;
            yy = Y_MAX - (H_FSB9) + 2;
            g_display.setFont(&FreeSansBold9pt7b);
            g_display.setCursor(xx, yy);
            g_display.println("1,7-Up,Down #-Do");
        }
        while (g_display.nextPage());
        
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("display_bip39 saw " + String(key));
        switch (key) {
        case '1':
            if (selected > 0)
                selected -= 1;
            break;
        case '7':
            if (selected < g_num_restore_shares + 1 + showrestore - 1)
                selected += 1;
            break;
        case '#':
            if (selected < g_num_restore_shares) {
                // Edit existing share
                g_selected_share = selected;
                g_uistate = ENTER_SHARE;
                return;
            } else if (selected == g_num_restore_shares) {
                // Add new share
                g_selected_share = g_num_restore_shares;
                g_restore_shares[g_selected_share] = NULL;
                g_num_restore_shares += 1;
                g_uistate = ENTER_SHARE;
                return;
            } else {
                // Attempt restoration
                g_uistate = SEEDLESS_MENU;	// FIXME - replace w real stuff.
                return;
            }
            break;
        default:
            break;
        }
    }
}

void enter_share() {
    if (g_restore_shares[g_selected_share])
        free(g_restore_shares[g_selected_share]);
    g_restore_shares[g_selected_share] =
        strdup("eraser senior decision roster beard "
               "treat identify grumpy salt index "
               "fake aviation theater cubic bike "
               "cause research dragon emphasis counter");
    g_uistate = RESTORE_SLIP39;
}

// ----------------------------------------------------------------

void recover_slip39() {
    display_words();
    Serial.println("reading keypad");
    char key;
    do {
        key = g_keypad.getKey();
    } while (key == NO_KEY);
    Serial.println("keypad saw " + String(key));
    switch (key) {
    case NO_KEY:
        break;
    case '4':
        move_left();
        break;
    case '6':
        move_right();
        break;
    case '2':
        jump_down();
        break;
    case '8':
        jump_up();
        break;
    case '1':
        prev_entry();
        break;
    case '7':
        next_entry();
        break;
    case '5':
        next_entry();
        break;
    default:
        break;
    }
}

String prefix(int word) {
    return String(slip39_wordlist[word]).substring(0, g_pos);
}

char current(int word) {
    return slip39_wordlist[word][g_pos];
}

bool unique_match() {
    // Does the previous word also match?
    if (g_wordndx[g_ndx] > 0)
        if (prefix(g_wordndx[g_ndx] - 1) == prefix(g_wordndx[g_ndx]))
            return false;
    // Does the next word also match?
    if (g_wordndx[g_ndx] < 1023)
        if (prefix(g_wordndx[g_ndx] + 1) == prefix(g_wordndx[g_ndx]))
            return false;
    return true;
}

void move_left() {
    if (g_pos >= 1)
        --g_pos;
}

void move_right() {
    if (g_pos < strlen(slip39_wordlist[g_wordndx[g_ndx]]) - 1)
        ++g_pos;
}

void jump_down() {
    // Find the previous word that differs in the cursor position.
    int wordndx0 = g_wordndx[g_ndx];	// remember starting wordndx
    String prefix0 = prefix(g_wordndx[g_ndx]);
    char curr0 = current(g_wordndx[g_ndx]);
    do {
        if (g_wordndx[g_ndx] == 0)
            g_wordndx[g_ndx] = 1023;
        else
            --g_wordndx[g_ndx];
        // If we've returned to the original there are no choices.
        if (g_wordndx[g_ndx] == wordndx0)
            break;
    } while (prefix(g_wordndx[g_ndx]) != prefix0 ||
             current(g_wordndx[g_ndx]) == curr0);
}

void jump_up() {
    // Find the next word that differs in the cursor position.
    int wordndx0 = g_wordndx[g_ndx];	// remember starting wordndx
    String prefix0 = prefix(g_wordndx[g_ndx]);
    char curr0 = current(g_wordndx[g_ndx]);
    do {
        if (g_wordndx[g_ndx] == 1023)
            g_wordndx[g_ndx] = 0;
        else
            ++g_wordndx[g_ndx];
        // If we've returned to the original there are no choices.
        if (g_wordndx[g_ndx] == wordndx0)
            break;
    } while (prefix(g_wordndx[g_ndx]) != prefix0 ||
             current(g_wordndx[g_ndx]) == curr0);
}

void prev_entry() {
    if (g_ndx == 0)
        g_ndx = 19;
    else
        --g_ndx;
    g_pos = 0;
}

void next_entry() {
    if (g_ndx == 19)
        g_ndx = 0;
    else
        ++g_ndx;
    g_pos = 0;
}

void display_words() {
    int xoff = 16;
    // int yoff = 95;
    int yoff = 25;
    int width = 14;
    int height = 20;
    int yborder = 4;
    int nrows = 6;

    compute_scroll();
    
    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        // g_display.fillScreen(GxEPD_WHITE);
        g_display.setFont(&FreeMonoBold12pt7b);

        for (int rr = 0; rr < nrows; ++rr) {
            int wndx = g_scroll + rr;
            String word = String(slip39_wordlist[g_wordndx[wndx]]);
            Serial.println(String(wndx) + " " + word);

            int yy = yoff + (rr * (height + yborder));
            
            if (wndx != g_ndx) {
                // Regular entry, not being edited
                g_display.setTextColor(GxEPD_BLACK);
                g_display.setCursor(xoff, yy);
                g_display.printf("%2d %s\n", wndx+1, word.c_str());
            } else {
                // Edited entry
                if (unique_match()) {
                    // Unique, highlight entire word.
                    g_display.fillRect(xoff,
                                       yy - height + yborder,
                                       width * (word.length() + 3),
                                       height,
                                       GxEPD_BLACK);
        
                    g_display.setTextColor(GxEPD_WHITE);
                    g_display.setCursor(xoff, yoff);

                    g_display.printf("%2d %s\n", wndx+1, word.c_str());

                } else {
                    // Not unique, highlight cursor.
                    g_display.setTextColor(GxEPD_BLACK);
                    g_display.setCursor(xoff, yy);
            
                    g_display.printf("%2d %s\n", wndx+1, word.c_str());

                    g_display.fillRect(xoff + (g_pos+3)*width,
                                       yy - height + yborder,
                                       width, height,
                                       GxEPD_BLACK);
        
                    g_display.setTextColor(GxEPD_WHITE);
                    g_display.setCursor(xoff + (g_pos+3)*width, yy);
                    g_display.printf("%c", word.c_str()[g_pos]);
                }
            }
        }
        
    }
    while (g_display.nextPage());
}

void compute_scroll() {
    if (g_ndx < 3)
        g_scroll = 0;
    else if (g_ndx > 17)
        g_scroll = 14;
    else
        g_scroll = g_ndx - 3;
}

void make_bip39_key() {
    if (g_rolls.length() * 2.5850 >= 128.0) {
        digitalWrite(26, HIGH);
    } else {
        digitalWrite(26, LOW);
    }
    
    display_status();
        
    if (!g_submitted) {
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("keypad saw " + String(key));

        switch (key) {
        case NO_KEY:
            break;
        case 'A': case 'B': case 'C': case 'D':
            break;
        case '1': case '2': case '3':
        case '4': case '5': case '6':
            g_rolls += key;
            break;
        case '*':
            g_rolls = "";
            break;
        case '#':
            g_submitted = true;
            generate_key();
            break;
        default:
            break;
        }
        Serial.println("g_rolls: " + g_rolls);
    } else {
        display_wordlist();

        make_slip39_wordlist();
        
        // Wait for a keypress
        char key;
        do {
            key = g_keypad.getKey();
        } while (key == NO_KEY);
        Serial.println("keypad saw " + String(key));

        reset_state();
    }
}

void no_input_or_display() {
    g_rolls = "123456";
    
    generate_key();
            
    for (int ndx = 0; ndx < 12; ndx += 2) {
        int col = 15;
        int row = ndx*10 + 40;
        String word0 = g_bip39.getMnemonic(g_bip39.getWord(ndx));
        String word1 = g_bip39.getMnemonic(g_bip39.getWord(ndx+1));
        Serial.println(word0 + " " + word1);
    }
    
    make_slip39_wordlist();
}

void generate_key() {
    Sha256Class sha256;
    sha256.init();
    for(uint8_t ii=0; ii < g_rolls.length(); ii++) {
        sha256.write(g_rolls[ii]);
    }
    memcpy(g_master_secret, sha256.result(), sizeof(g_master_secret));
    g_bip39.setPayloadBytes(sizeof(g_master_secret));
    g_bip39.setPayload(sizeof(g_master_secret), (uint8_t *)g_master_secret);
    for (int ndx = 0; ndx < 12; ++ndx) {
        uint16_t word = g_bip39.getWord(ndx);
        Serial.println(String(ndx) + " " + String(g_bip39.getMnemonic(word)));
    }
}

void display_status() {
    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        // g_display.fillScreen(GxEPD_WHITE);
        g_display.setTextColor(GxEPD_BLACK);
        
        g_display.setFont(&FreeSansBold12pt7b);
        g_display.setCursor(2, 30);
        g_display.println("BIP39 Generator");
        g_display.setCursor(2, 60);
        g_display.println("Enter Dice Rolls");
        
        g_display.setFont(&FreeMonoBold12pt7b);
        g_display.setCursor(10, 95);
        g_display.printf("Rolls: %d\n", g_rolls.length());
        g_display.setCursor(10, 123);
        g_display.printf(" Bits: %0.1f\n", g_rolls.length() * 2.5850);
        
        g_display.setFont(&FreeSansBold9pt7b);
        g_display.setCursor(0, 160);
        g_display.println("   Press * to clear");
        g_display.println("   Press # to submit");
    }
    while (g_display.nextPage());
}

void display_wordlist() {
    g_display.firstPage();
    do
    {
        g_display.setPartialWindow(0, 0, 200, 200);
        // g_display.fillScreen(GxEPD_WHITE);
        g_display.setFont(&FreeSansBold9pt7b);
        for (int ndx = 0; ndx < 12; ndx += 2) {
            int col = 15;
            int row = ndx*10 + 40;
            String word0 = g_bip39.getMnemonic(g_bip39.getWord(ndx));
            String word1 = g_bip39.getMnemonic(g_bip39.getWord(ndx+1));
            g_display.setCursor(col, row);
            g_display.printf("%s  %s", word0.c_str(), word1.c_str());
        }
        g_display.setFont(&FreeSansBold9pt7b);
        g_display.setCursor(20, 180);
        g_display.println("Press * to clear");
    }
    while (g_display.nextPage());
}

void make_slip39_wordlist() {
    int gt = 3;
    int gn = 5;
    
    char *ml[gn];
    
    for (int ii = 0; ii < gn; ++ii)
        ml[ii] = (char *)malloc(MNEMONIC_LIST_LEN);

    Serial.println("calling generate_mnemonics");
    generate_mnemonics(gt, gn,
                       g_master_secret, sizeof(g_master_secret),
                       NULL, 0, 0, ml);

    for (int ii = 0; ii < gn; ++ii)
        Serial.println(String(ii) + " " + String(ml[ii]));

    // Combine 2, 0, 4 to recover the secret.
    char *dml[5];
    dml[0] = ml[2];
    dml[1] = ml[0];
    dml[2] = ml[4];
    
    uint8_t ms[16];
    int msl;
    msl = sizeof(ms);
    combine_mnemonics(gt, dml, NULL, 0, ms, &msl);

    for (int ii = 0; ii < gn; ++ii)
        free(ml[ii]);
    
    if (msl == sizeof(g_master_secret) &&
        memcmp(g_master_secret, ms, msl) == 0) {
        Serial.println("SUCCESS");
    } else {
        Serial.println("FAIL");
    }
}

void verify_slip39_multilevel() {
    int gt = 5;
    char* ml[5] = {
                      "eraser senior decision roster beard "
                      "treat identify grumpy salt index "
                      "fake aviation theater cubic bike "
                      "cause research dragon emphasis counter",

                      "eraser senior ceramic snake clay "
                      "various huge numb argue hesitate "
                      "auction category timber browser greatest "
                      "hanger petition script leaf pickup",
                      
                      "eraser senior ceramic shaft dynamic "
                      "become junior wrist silver peasant "
                      "force math alto coal amazing "
                      "segment yelp velvet image paces",
#if 0
                      // Duplicate of the prior, should fail.
                      "eraser senior ceramic shaft dynamic "
                      "become junior wrist silver peasant "
                      "force math alto coal amazing "
                      "segment yelp velvet image paces",
#else
                      // This one works
                      "eraser senior ceramic round column "
                      "hawk trust auction smug shame "
                      "alive greatest sheriff living perfect "
                      "corner chest sled fumes adequate",
#endif
                      
                      "eraser senior decision smug corner "
                      "ruin rescue cubic angel tackle "
                      "skin skunk program roster trash "
                      "rumor slush angel flea amazing",
    };

    uint8_t ms[16];
    int msl;
    msl = sizeof(ms);
    int rc = combine_mnemonics(gt, (char**) ml, NULL, 0, ms, &msl);

    if (rc == 0) {
        Serial.println("VERIFIED " + String(rc));
    } else {
        Serial.println("FAILED " + String(rc));
    }
}


extern "C" {
void random_buffer(uint8_t *buf, size_t len) {
    printf("random_buffer %d\n", len);
    uint32_t r = 0;
    for (size_t i = 0; i < len; i++) {
        if (i % 4 == 0) {
#if defined(ESP32)
            r = esp_random();
#elif defined(SAMD51)
            r = trngGetRandomNumber();
#endif
        }
        buf[i] = (r >> ((i % 4) * 8)) & 0xFF;
    }
}

void debug_print(char const * str) {
    Serial.println(str);
}
}
