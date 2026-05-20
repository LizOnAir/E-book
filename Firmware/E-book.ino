#include <WiFi.h>
#include <HTTPClient.h>
#include <FS.h>
#include <SPIFFS.h>
#include <GxEPD2_BW.h>
#include <Adafruit_GFX.h>

// Wi-Fi Settings
const char* ssid = "YOUR_WIFI_SSID";
const char* password = "YOUR_WIFI_PASSWORD";
const char* bookServerUrl = "http://your-server-ip/books/book1.txt"; //example

// Pin Definitions
#define BUTTON_PIN 14
#define EPD_CS     5
#define EPD_DC     17
#define EPD_RST    16
#define EPD_BUSY   4

// E-Ink Display Setup (7.5 inch GDEH075Z90) 
GxEPD2_BW<GxEPD2_750_GDEHY075TT1, GxEPD2_750_GDEHY075TT1::HEIGHT> display(GxEPD2_750_GDEHY075TT1(EPD_CS, EPD_DC, EPD_RST, EPD_BUSY));

// State
enum AppState { MENU, READING, DOWNLOADING };
AppState currentState = MENU;

//  Menu 
const int totalMenuOptions = 3;
const char* menuOptions[totalMenuOptions] = {
  "1. Read Book",
  "2. Download New Book",
  "3. Load Bookmark"
};
int currentMenuSelection = 0;

// Button timing for Short vs Long press
unsigned long buttonPressStartTime = 0;
const unsigned long longPressDuration = 800; // milliseconds for select
bool lastButtonState = HIGH;

//  Function Prototypes 
void initDisplay();
void drawMenu();
void handleButton();
void downloadBook();
void readBook(bool lookupBookmark);
void saveBookmark(int pagePosition);
int loadBookmark();

void setup() {
  Serial.begin(115200);
  pinMode(BUTTON_PIN, INPUT_PULLUP);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS Mount Failed");
    return;
  }

  initDisplay();
  drawMenu();
}

void loop() {
  handleButton();
  delay(10); // Debounce / stability delay
}

// Display Initialization 
void initDisplay() {
  display.init(115200);
  display.setRotation(1);
  display.setTextColor(GxEPD_BLACK);
  display.setBackgroundColor(GxEPD_WHITE);
}

// UI Rendering 
void drawMenu() {
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextSize(3);
    display.setCursor(50, 50);
    display.println(" ESP32 E-READER ");
    display.drawFastHLine(50, 70, 700, GxEPD_BLACK);

    display.setTextSize(2);
    for (int i = 0; i < totalMenuOptions; i++) {
      int yPos = 140 + (i * 60);
      if (i == currentMenuSelection) {
        display.fillRect(45, yPos - 10, 600, 40, GxEPD_BLACK);
        display.setTextColor(GxEPD_WHITE);
      } else {
        display.setTextColor(GxEPD_BLACK);
      }
      display.setCursor(60, yPos);
      display.println(menuOptions[i]);
    }
  } while (display.nextPage());
}

// Button Handling (Multi-click & Long Press) 
void handleButton() {
  bool currentButtonState = digitalRead(BUTTON_PIN);

  // Button Pressed Down
  if (lastButtonState == HIGH && currentButtonState == LOW) {
    buttonPressStartTime = millis();
  }
  
  // Button Released
  if (lastButtonState == LOW && currentButtonState == HIGH) {
    unsigned long pressDuration = millis() - buttonPressStartTime;

    if (pressDuration >= longPressDuration) {
      // LONG PRESS -> TRIGGER ACTION
      executeSelection();
    } else {
      // SHORT PRESS -> NEXT
      if (currentState == MENU) {
        currentMenuSelection = (currentMenuSelection + 1) % totalMenuOptions;
        drawMenu();
      } else {
        // If reading, short press acts as turning the page or returning to menu
        currentState = MENU;
        drawMenu();
      }
    }
  }
  lastButtonState = currentButtonState;
}

//  Menu Action Executor 
void executeSelection() {
  if (currentState == MENU) {
    if (currentMenuSelection == 0) {
      readBook(false); // Read from beginning
    } else if (currentMenuSelection == 1) {
      downloadBook();
    } else if (currentMenuSelection == 2) {
      readBook(true);  // Read from saved bookmark
    }
  }
}

// WiFi & Network Operations 
void downloadBook() {
  currentState = DOWNLOADING;
  Serial.println("Connecting to WiFi...");
  
  WiFi.begin(ssid, password);
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    attempts++;
  }

  if (WiFi.status() != WL_CONNECTED) {
    Serial.println("WiFi Connection Failed");
    currentState = MENU;
    drawMenu();
    return;
  }

  HTTPClient http;
  http.begin(bookServerUrl);
  int httpCode = http.GET();

  if (httpCode == HTTP_CODE_OK) {
    File file = SPIFFS.open("/book.txt", FILE_WRITE);
    if (file) {
      http.writeToStream(&file);
      file.close();
      Serial.println("Book downloaded successfully!");
    }
  } else {
    Serial.printf("HTTP GET failed, error: %s\n", http.errorToString(httpCode).c_str());
  }
  
  http.end();
  WiFi.disconnect(true);
  
  currentState = MENU;
  drawMenu();
}

// Storage & Book Reading Operations 
void readBook(bool lookupBookmark) {
  currentState = READING;
  
  if (!SPIFFS.exists("/book.txt")) {
    Serial.println("No book found. Please download one first.");
    currentState = MENU;
    drawMenu();
    return;
  }

  File file = SPIFFS.open("/book.txt", FILE_READ);
  int startPosition = 0;

  if (lookupBookmark) {
    startPosition = loadBookmark();
    file.seek(startPosition);
  }

  // Read a chunk of text to display on the screen
  String bookPage = "";
  for (int i = 0; i < 500 && file.available(); i++) {
    bookPage += (char)file.read();
  }
  
  int nextBookmarkPos = file.position();
  file.close();

  // Save progress automatically
  saveBookmark(nextBookmarkPos);

  // Render Page to E-Ink
  display.setFullWindow();
  display.firstPage();
  do {
    display.fillScreen(GxEPD_WHITE);
    display.setTextColor(GxEPD_BLACK);
    display.setTextSize(2);
    display.setCursor(30, 40);
    display.println(" Reading: book.txt ");
    display.setCursor(30, 90);
    display.println(bookPage.c_str());
    display.setTextSize(1);
    display.setCursor(30, 450);
    display.print("[Short Press to Exit | Progress Saved]");
  } while (display.nextPage());
}

void saveBookmark(int pagePosition) {
  File bookmarkFile = SPIFFS.open("/bookmark.txt", FILE_WRITE);
  if (bookmarkFile) {
    bookmarkFile.print(pagePosition);
    bookmarkFile.close();
    Serial.printf("Bookmark saved at byte: %d\n", pagePosition);
  }
}

int loadBookmark() {
  if (!SPIFFS.exists("/bookmark.txt")) return 0;
  
  File bookmarkFile = SPIFFS.open("/bookmark.txt", FILE_READ);
  if (bookmarkFile) {
    String posStr = bookmarkFile.readString();
    bookmarkFile.close();
    return posStr.toInt();
  }
  return 0;
}
