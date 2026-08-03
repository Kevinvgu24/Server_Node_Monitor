#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <MCUFRIEND_kbv.h>
#include <TouchScreen.h>

// Initialize MCUFRIEND TFT Screen
MCUFRIEND_kbv tft;

// Touch screen pin definitions for typical 3.5" MCUFRIEND LCD shields
const int XP = 8;   // LCD_RST / D8
const int XM = A2;  // LCD_RS / A2
const int YP = A3;  // LCD_CS / A3
const int YM = 9;   // LCD_D1 / D9

// Touch calibration values
const int TS_LEFT = 900;
const int TS_RT = 120;
const int TS_TOP = 90;
const int TS_BOT = 910;

#define MINPRESSURE 10
#define MAXPRESSURE 1000

TouchScreen ts = TouchScreen(XP, YP, XM, YM, 300);

// Color Palette (Sleek Dark Mode)
#define COLOR_BG        0x0000 // Black
#define COLOR_CARD      0x10A2 // Very Dark Grey
#define COLOR_TEXT_PRI  0xFFFF // White
#define COLOR_TEXT_SEC  0xBDF7 // Light Grey
#define COLOR_BORDER    0x3186 // Dark Slate Blue
#define COLOR_CYAN      0x07FF // Cyan (CPU / SSD / Workloads)
#define COLOR_GREEN     0x07E0 // Emerald Green (RAM / Active)
#define COLOR_YELLOW    0xFFE0 // Yellow (Disk / HDD / Node)
#define COLOR_RED       0xF800 // Red (Alarm)
#define COLOR_ORANGE    0xFD20 // Orange (Stopped VMs / Hot Disk)
#define COLOR_HEADER    0x0842 // Very Dark Blue/Grey

// System metrics variables
int cpuUsage = 0;
int ramUsage = 0;
float cpuTemp = 0.0;
int vmRunning = 0;
int vmStopped = 0;
int ctRunning = 0;
int ctStopped = 0;
int docRunning = 0;
int docStopped = 0;
char uptimeStr[16] = "N/A";

// SSD Metric (1 SSD)
char ssdName[12] = "None";
char ssdSize[10] = "0G";
int ssdUsage = -1;
int ssdTemp = -1;

// HDD Metrics (up to 6 HDDs)
struct DiskDrive {
    char name[12];
    char size[10];
    int usage;
    int temp;
};
DiskDrive hddList[6];

// Connection and Page States
bool connected = false;
unsigned long lastPacketTime = 0;
uint8_t currentPage = 0;
uint8_t prevPage = 99;
unsigned long lastTouchTime = 0;
bool dataReceived = false;

// Serial Buffer (Expanded to 256 bytes for 38-field packet)
char serialBuf[256];
uint16_t bufIdx = 0;

void drawHeader();
void drawPageIndicators();
void parsePacket(char* data);
void handleTouch();
void updateScreen();
void drawPage0Static();
void updatePage0Data();
void drawPage1Static();
void updatePage1Data();
void drawPage2Static();
void updatePage2Data();

void setup() {
    Serial.begin(115200);
    
    // Initialize default HDD names to None
    for (int i = 0; i < 6; i++) {
        strcpy(hddList[i].name, "None");
        strcpy(hddList[i].size, "0G");
        hddList[i].usage = -1;
        hddList[i].temp = -1;
    }
    
    // Initialize TFT
    uint16_t identifier = tft.readID();
    if (identifier == 0x0154) identifier = 0x9341;
    if (identifier == 0x0000) identifier = 0x9486;
    tft.begin(identifier);
    tft.setRotation(1); // Landscape mode (480x320)
    tft.fillScreen(COLOR_BG);
    
    drawHeader();
}

void loop() {
    while (Serial.available() > 0) {
        char c = Serial.read();
        if (c == '\n' || c == '\r') {
            if (bufIdx > 0) {
                serialBuf[bufIdx] = '\0';
                parsePacket(serialBuf);
                bufIdx = 0;
                lastPacketTime = millis();
                dataReceived = true;
                if (!connected) {
                    connected = true;
                    drawHeader();
                }
            }
        } else {
            if (bufIdx < sizeof(serialBuf) - 1) {
                serialBuf[bufIdx++] = c;
            }
        }
    }
    
    if (connected && (millis() - lastPacketTime > 10000)) {
        connected = false;
        drawHeader();
        dataReceived = true;
    }
    
    handleTouch();
    updateScreen();
    delay(50);
}

void drawHeader() {
    tft.fillRect(0, 0, 480, 40, COLOR_HEADER);
    tft.drawFastHLine(0, 40, 480, COLOR_BORDER);
    
    tft.setTextColor(COLOR_TEXT_PRI);
    tft.setTextSize(2);
    tft.setCursor(15, 12);
    tft.print(F("PVE NODE MONITOR"));
    
    if (connected) {
        tft.fillCircle(455, 20, 6, COLOR_GREEN);
    } else {
        tft.fillCircle(455, 20, 6, COLOR_RED);
    }
    
    drawPageIndicators();
}

void drawPageIndicators() {
    int startX = 220;
    int y = 310;
    for (int i = 0; i < 3; i++) {
        if (i == currentPage) {
            tft.fillCircle(startX + i * 20, y, 4, COLOR_CYAN);
        } else {
            tft.fillCircle(startX + i * 20, y, 3, COLOR_BORDER);
        }
    }
}

// Parse Packet (38 fields):
// CPU|RAM|CPU_TEMP|SSD_Name|SSD_Size|SSD_Usage|SSD_Temp|HDD1..6(24 fields)|VM_RUN|VM_STOP|CT_RUN|CT_STOP|DOC_RUN|DOC_STOP|UPTIME
void parsePacket(char* data) {
    int fieldIdx = 0;
    char* start = data;
    char* end;
    
    while (fieldIdx < 38) {
        end = strchr(start, '|');
        if (end) {
            *end = '\0';
        }
        
        char* token = start;
        
        switch (fieldIdx) {
            case 0: cpuUsage = atoi(token); break;
            case 1: ramUsage = atoi(token); break;
            case 2: cpuTemp = atof(token); break;
            
            // SSD (1 drive)
            case 3: strncpy(ssdName, token, sizeof(ssdName) - 1); ssdName[sizeof(ssdName) - 1] = '\0'; break;
            case 4: strncpy(ssdSize, token, sizeof(ssdSize) - 1); ssdSize[sizeof(ssdSize) - 1] = '\0'; break;
            case 5: ssdUsage = (token[0] != '\0') ? atoi(token) : -1; break;
            case 6: ssdTemp = (token[0] != '\0') ? atoi(token) : -1; break;
            
            // HDDs (6 drives, fields 7 to 30)
            default:
                if (fieldIdx >= 7 && fieldIdx <= 30) {
                    int hddIdx = (fieldIdx - 7) / 4;
                    int subIdx = (fieldIdx - 7) % 4;
                    switch (subIdx) {
                        case 0: strncpy(hddList[hddIdx].name, token, sizeof(hddList[hddIdx].name) - 1); hddList[hddIdx].name[sizeof(hddList[hddIdx].name) - 1] = '\0'; break;
                        case 1: strncpy(hddList[hddIdx].size, token, sizeof(hddList[hddIdx].size) - 1); hddList[hddIdx].size[sizeof(hddList[hddIdx].size) - 1] = '\0'; break;
                        case 2: hddList[hddIdx].usage = (token[0] != '\0') ? atoi(token) : -1; break;
                        case 3: hddList[hddIdx].temp = (token[0] != '\0') ? atoi(token) : -1; break;
                    }
                } else if (fieldIdx == 31) {
                    vmRunning = atoi(token);
                } else if (fieldIdx == 32) {
                    vmStopped = atoi(token);
                } else if (fieldIdx == 33) {
                    ctRunning = atoi(token);
                } else if (fieldIdx == 34) {
                    ctStopped = atoi(token);
                } else if (fieldIdx == 35) {
                    docRunning = atoi(token);
                } else if (fieldIdx == 36) {
                    docStopped = atoi(token);
                } else if (fieldIdx == 37) {
                    int len = strlen(token);
                    while (len > 0 && (token[len-1] == '\r' || token[len-1] == '\n' || token[len-1] == ' ')) {
                        token[--len] = '\0';
                    }
                    strncpy(uptimeStr, token, sizeof(uptimeStr) - 1);
                    uptimeStr[sizeof(uptimeStr) - 1] = '\0';
                }
                break;
        }
        
        if (!end) break;
        start = end + 1;
        fieldIdx++;
    }
}

void handleTouch() {
    if (millis() - lastTouchTime < 300) return;
    
    TSPoint p = ts.getPoint();
    pinMode(XM, OUTPUT);
    pinMode(YP, OUTPUT);
    
    if (p.z > MINPRESSURE && p.z < MAXPRESSURE) {
        int16_t screen_y = map(p.x, TS_TOP, TS_BOT, 0, 320);
        if (screen_y > 40 && screen_y < 300) {
            currentPage = (currentPage + 1) % 3;
            lastTouchTime = millis();
        }
    }
}

void updateScreen() {
    bool pageChanged = (currentPage != prevPage);
    
    if (pageChanged) {
        tft.fillRect(0, 41, 480, 260, COLOR_BG);
        drawPageIndicators();
        
        switch (currentPage) {
            case 0: drawPage0Static(); break;
            case 1: drawPage1Static(); break;
            case 2: drawPage2Static(); break;
        }
        prevPage = currentPage;
    }
    
    if (dataReceived || pageChanged) {
        switch (currentPage) {
            case 0: updatePage0Data(); break;
            case 1: updatePage1Data(); break;
            case 2: updatePage2Data(); break;
        }
        dataReceived = false;
    }
}

// ================= PAGE 0: SYSTEM OVERVIEW =================

void drawPage0Static() {
    tft.setTextColor(COLOR_TEXT_SEC);
    tft.setTextSize(2);
    
    tft.drawRoundRect(15, 55, 215, 110, 8, COLOR_BORDER);
    tft.setCursor(30, 65);
    tft.print(F("CPU LOAD"));
    
    tft.drawRoundRect(15, 180, 215, 110, 8, COLOR_BORDER);
    tft.setCursor(30, 190);
    tft.print(F("RAM USAGE"));
    
    tft.drawRoundRect(250, 55, 215, 235, 8, COLOR_BORDER);
    tft.setCursor(265, 65);
    tft.print(F("SYSTEM INFO"));
    
    tft.setTextSize(1);
    tft.setCursor(265, 100);
    tft.print(F("SYSTEM DISK USAGE"));
    tft.setCursor(265, 190);
    tft.print(F("SYSTEM UPTIME"));
}

void updatePage0Data() {
    char buf[20];
    
    tft.fillRect(30, 95, 185, 32, COLOR_BG);
    tft.fillRect(30, 220, 185, 32, COLOR_BG);
    tft.fillRect(265, 115, 185, 24, COLOR_BG);
    tft.fillRect(265, 210, 185, 20, COLOR_BG);
    
    // 1. CPU
    tft.setTextSize(4);
    tft.setTextColor(COLOR_CYAN, COLOR_BG);
    tft.setCursor(30, 95);
    snprintf(buf, sizeof(buf), "%d%%", cpuUsage);
    tft.print(buf);
    
    tft.setTextSize(2);
    tft.setCursor(135, 105);
    if (cpuTemp > 0.1) {
        if (cpuTemp > 75.0) tft.setTextColor(COLOR_RED, COLOR_BG);
        else if (cpuTemp > 60.0) tft.setTextColor(COLOR_YELLOW, COLOR_BG);
        else tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
        dtostrf(cpuTemp, 4, 1, buf);
        strcat(buf, "C");
        tft.print(buf);
    } else {
        tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
        tft.print(F("--.-C"));
    }
    
    tft.fillRect(30, 140, 180, 8, COLOR_BG);
    tft.drawRect(30, 140, 180, 8, COLOR_BORDER);
    int cpuWidth = (cpuUsage * 176) / 100;
    tft.fillRect(32, 142, cpuWidth, 4, COLOR_CYAN);
    
    // 2. RAM
    tft.setTextSize(4);
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.setCursor(30, 220);
    snprintf(buf, sizeof(buf), "%3d%%", ramUsage);
    tft.print(buf);
    
    tft.fillRect(30, 265, 180, 8, COLOR_BG);
    tft.drawRect(30, 265, 180, 8, COLOR_BORDER);
    int ramWidth = (ramUsage * 176) / 100;
    tft.fillRect(32, 267, ramWidth, 4, COLOR_GREEN);
    
    // 3. Storage Metric
    int disk1Usage = (strcmp(ssdName, "None") != 0) ? ssdUsage : hddList[0].usage;
    uint16_t disk1Color = (strcmp(ssdName, "None") != 0) ? COLOR_CYAN : COLOR_YELLOW;
    
    tft.setTextSize(3);
    tft.setTextColor(disk1Color, COLOR_BG);
    tft.setCursor(265, 115);
    if (disk1Usage >= 0) {
        snprintf(buf, sizeof(buf), "%d%% Used", disk1Usage);
    } else {
        snprintf(buf, sizeof(buf), "Unused  ");
    }
    tft.print(buf);
    
    tft.fillRect(265, 150, 180, 8, COLOR_BG);
    tft.drawRect(265, 150, 180, 8, COLOR_BORDER);
    int diskWidth = (disk1Usage >= 0 ? disk1Usage : 0) * 176 / 100;
    tft.fillRect(267, 152, diskWidth, 4, disk1Color);
    
    // 4. Uptime
    tft.setTextSize(2);
    tft.setTextColor(COLOR_TEXT_PRI, COLOR_BG);
    tft.setCursor(265, 210);
    snprintf(buf, sizeof(buf), "%-14s", uptimeStr);
    tft.print(buf);
}

// ================= PAGE 1: DISK TEMPERATURES & STORAGE (1 SSD + 6 HDDs) =================

void drawPage1Static() {
    tft.setTextColor(COLOR_TEXT_SEC);
    tft.setTextSize(2);
    
    // Left Box: SSD Drive (x=15, y=55, w=205, h=235)
    tft.drawRoundRect(15, 55, 205, 235, 8, COLOR_BORDER);
    tft.setCursor(30, 65);
    tft.setTextColor(COLOR_CYAN);
    tft.print(F("SSD DRIVE"));
    
    // Right Box: HDD Drives & Temps (x=235, y=55, w=230, h=235)
    tft.drawRoundRect(235, 55, 230, 235, 8, COLOR_BORDER);
    tft.setCursor(250, 65);
    tft.setTextColor(COLOR_YELLOW);
    tft.print(F("HDD STORAGE"));
    
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_SEC);
    tft.setCursor(30, 100);
    tft.print(F("SYSTEM DISK (SSD)"));
    tft.setCursor(30, 195);
    tft.print(F("SSD TEMPERATURE"));
}

void updatePage1Data() {
    char buf[20];
    
    // 1. SSD SECTION
    tft.fillRect(30, 115, 185, 24, COLOR_BG);
    tft.fillRect(30, 215, 185, 32, COLOR_BG);
    
    if (strcmp(ssdName, "None") != 0) {
        tft.setTextSize(1);
        tft.setTextColor(COLOR_TEXT_PRI, COLOR_BG);
        tft.setCursor(30, 115);
        snprintf(buf, sizeof(buf), "%s [%s]", ssdName, ssdSize);
        tft.print(buf);
        
        tft.setTextSize(3);
        tft.setTextColor(COLOR_CYAN, COLOR_BG);
        tft.setCursor(30, 130);
        if (ssdUsage >= 0) {
            snprintf(buf, sizeof(buf), "%d%% Used", ssdUsage);
        } else {
            snprintf(buf, sizeof(buf), "Unused");
        }
        tft.print(buf);
        
        tft.fillRect(30, 165, 180, 8, COLOR_BG);
        tft.drawRect(30, 165, 180, 8, COLOR_BORDER);
        int ssdWidth = (ssdUsage >= 0 ? ssdUsage : 0) * 176 / 100;
        tft.fillRect(32, 167, ssdWidth, 4, COLOR_CYAN);
        
        tft.setTextSize(4);
        if (ssdTemp > 0) {
            if (ssdTemp >= 60) tft.setTextColor(COLOR_RED, COLOR_BG);
            else if (ssdTemp >= 50) tft.setTextColor(COLOR_ORANGE, COLOR_BG);
            else tft.setTextColor(COLOR_CYAN, COLOR_BG);
            snprintf(buf, sizeof(buf), "%dC", ssdTemp);
        } else {
            tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
            snprintf(buf, sizeof(buf), "--C");
        }
        tft.setCursor(30, 215);
        tft.print(buf);
    } else {
        tft.setTextSize(2);
        tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
        tft.setCursor(30, 120);
        tft.print(F("No SSD"));
        tft.setCursor(30, 215);
        tft.print(F("N/A"));
    }
    
    // 2. HDD SECTION (Up to 6 HDDs)
    for (int i = 0; i < 6; i++) {
        int y = 92 + i * 31;
        tft.fillRect(248, y, 210, 28, COLOR_BG);
        
        if (strcmp(hddList[i].name, "None") != 0 && hddList[i].name[0] != '\0') {
            tft.setTextSize(1);
            tft.setTextColor(COLOR_TEXT_PRI, COLOR_BG);
            tft.setCursor(250, y + 2);
            char label[20];
            snprintf(label, sizeof(label), "%s [%s]", hddList[i].name, hddList[i].size);
            tft.print(label);
            
            tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
            tft.setCursor(250, y + 14);
            if (hddList[i].usage >= 0) {
                snprintf(label, sizeof(label), "%2d%%", hddList[i].usage);
            } else {
                snprintf(label, sizeof(label), "--%%");
            }
            tft.print(label);
            
            tft.drawRect(280, y + 15, 95, 7, COLOR_BORDER);
            if (hddList[i].usage >= 0) {
                int fillW = (hddList[i].usage * 91) / 100;
                tft.fillRect(282, y + 17, fillW, 3, COLOR_YELLOW);
            }
            
            tft.setTextSize(2);
            if (hddList[i].temp > 0) {
                if (hddList[i].temp >= 55) tft.setTextColor(COLOR_RED, COLOR_BG);
                else if (hddList[i].temp >= 45) tft.setTextColor(COLOR_ORANGE, COLOR_BG);
                else tft.setTextColor(COLOR_YELLOW, COLOR_BG);
                snprintf(label, sizeof(label), "%2dC", hddList[i].temp);
            } else {
                tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
                snprintf(label, sizeof(label), "--C");
            }
            tft.setCursor(385, y + 5);
            tft.print(label);
        } else {
            tft.setTextSize(1);
            tft.setTextColor(COLOR_BORDER, COLOR_BG);
            tft.setCursor(250, y + 8);
            tft.print(F("- Empty Slot -"));
        }
    }
}

// ================= PAGE 2: WORKLOADS & NODE STATUS =================

void drawPage2Static() {
    tft.setTextColor(COLOR_TEXT_SEC);
    tft.setTextSize(2);
    
    // Left Box: Workloads & Services (x=15, y=55, w=215, h=235)
    tft.drawRoundRect(15, 55, 215, 235, 8, COLOR_BORDER);
    tft.setCursor(30, 65);
    tft.setTextColor(COLOR_CYAN);
    tft.print(F("WORKLOADS"));
    
    // Right Box: Node & Health Status (x=245, y=55, w=220, h=235)
    tft.drawRoundRect(245, 55, 220, 235, 8, COLOR_BORDER);
    tft.setCursor(260, 65);
    tft.setTextColor(COLOR_YELLOW);
    tft.print(F("NODE HEALTH"));
    
    // Workloads Subtitles (Left Box)
    tft.setTextSize(1);
    tft.setTextColor(COLOR_TEXT_SEC);
    tft.setCursor(30, 95);
    tft.print(F("QEMU VIRTUAL MACHINES"));
    tft.setCursor(30, 155);
    tft.print(F("LXC CONTAINERS"));
    tft.setCursor(30, 215);
    tft.print(F("DOCKER SERVICES"));
    
    // Node Health Subtitles (Right Box)
    tft.setCursor(260, 95);
    tft.print(F("PVE HYPERVISOR"));
    tft.setCursor(260, 145);
    tft.print(F("DATA AGENT"));
    tft.setCursor(260, 195);
    tft.print(F("TOTAL ACTIVE / OFF"));
}

void updatePage2Data() {
    char buf[20];
    
    // Clear dynamic areas
    tft.fillRect(30, 112, 190, 24, COLOR_BG);  // QEMU VM
    tft.fillRect(30, 172, 190, 24, COLOR_BG);  // LXC CT
    tft.fillRect(30, 232, 190, 24, COLOR_BG);  // Docker
    
    tft.fillRect(260, 112, 190, 20, COLOR_BG); // Hypervisor Status
    tft.fillRect(260, 162, 190, 20, COLOR_BG); // Agent Status
    tft.fillRect(260, 212, 190, 65, COLOR_BG); // Total Summary
    
    // 1. QEMU VMs
    tft.setTextSize(2);
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.setCursor(30, 112);
    snprintf(buf, sizeof(buf), "%2dA", vmRunning);
    tft.print(buf);
    
    if (vmStopped > 0) tft.setTextColor(COLOR_ORANGE, COLOR_BG);
    else tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
    tft.setCursor(120, 112);
    snprintf(buf, sizeof(buf), "| %2d Off", vmStopped);
    tft.print(buf);
    
    // 2. LXC CTs
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.setCursor(30, 172);
    snprintf(buf, sizeof(buf), "%2dA", ctRunning);
    tft.print(buf);
    
    if (ctStopped > 0) tft.setTextColor(COLOR_ORANGE, COLOR_BG);
    else tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
    tft.setCursor(120, 172);
    snprintf(buf, sizeof(buf), "| %2d Off", ctStopped);
    tft.print(buf);
    
    // 3. Docker Services (Only Active count displayed)
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.setCursor(30, 232);
    snprintf(buf, sizeof(buf), "%2d Active", docRunning);
    tft.print(buf);
    
    // 4. Hypervisor status
    tft.setTextSize(2);
    tft.setCursor(260, 112);
    if (connected) {
        tft.setTextColor(COLOR_GREEN, COLOR_BG);
        tft.print(F("ONLINE "));
    } else {
        tft.setTextColor(COLOR_RED, COLOR_BG);
        tft.print(F("OFFLINE"));
    }
    
    // 5. Agent connection status
    tft.setCursor(260, 162);
    if (connected) {
        tft.setTextColor(COLOR_GREEN, COLOR_BG);
        tft.print(F("RECEIVING  "));
    } else {
        tft.setTextColor(COLOR_RED, COLOR_BG);
        tft.print(F("NO SIGNAL  "));
    }
    
    // 6. Total Summary
    int totalRun = vmRunning + ctRunning + docRunning;
    int totalStop = vmStopped + ctStopped;
    
    tft.setTextSize(3);
    tft.setTextColor(COLOR_GREEN, COLOR_BG);
    tft.setCursor(260, 212);
    snprintf(buf, sizeof(buf), "%2d Active", totalRun);
    tft.print(buf);
    
    tft.setTextSize(2);
    if (totalStop > 0) tft.setTextColor(COLOR_ORANGE, COLOR_BG);
    else tft.setTextColor(COLOR_TEXT_SEC, COLOR_BG);
    tft.setCursor(260, 245);
    snprintf(buf, sizeof(buf), "%2d Stopped", totalStop);
    tft.print(buf);
}
