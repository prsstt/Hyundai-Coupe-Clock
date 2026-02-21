#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "ELMduino.h"
#include "bocchi_frames.h"
#include "hyundai_logo.h"
#include "menu_resources.h"
#include <EEPROM.h>

// --- KONFIGURACJA SPRZĘTU (XIAO RP2040) ---

// HC-05 Bluetooth:
// Używamy sprzętowego Serial1 (Piny: TX=D6, RX=D7 na XIAO RP2040)
#define BTSerial Serial1

// OLED SSD1306 (I2C):
// XIAO RP2040 -> OLED SCL (D5), OLED SDA (D4)
U8G2_SSD1306_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE);

ELM327 myELM327;

// --- KONFIGURACJA LOGO ---
#define LOGO_WIDTH  128
#define LOGO_HEIGHT 64
#define LOGO_DURATION 5000 // Czas wyświetlania logo w milisekundach (5s)

unsigned long logoStartTime = 0;
bool logoShown = false;

// Zmienne systemowe
unsigned long lastPageSwitch = 0;
int currentPage = 0;
const int totalPages = 2;
const int switchInterval = 4000; // Czas wyświetlania jednego ekranu (4s)
bool connected = false;

// Zmienne DANYCH
float vacuum = 0;   // Podciśnienie (Bar)
float map_val = 0;  // Pobrane z OBD (kPa)
float stft = 0;     // Korekta krótka
float ltft = 0;     // Korekta długa
float last_map_kpa = 100.0; // Zapamiętana wartość MAP (startowo 100 kPa - atmosferyczne)
int32_t last_speed = 0;     // Zapamiętana prędkość

// Zmienne MENU (Lista)
const char* menuItems[] = {
  "Animation",
  "Fuel Consumption",
  "Park Sensor",
  "GPS Gauge",
  "Dashboard",
  "Settings"
};
const int menuCount = 6;
int currentMenuIndex = 0;

// Zmienne Settings Menu
const char* settingsItems[] = {
  "Start Screen",
  "Menu Style",
  "Back"
};
const int settingsCount = 3;
int settingsMenuIndex = 0;

// Zmienne Start Screen Menu
int startScreenMenuIndex = 0;
int savedStartScreen = 4; // Domyślnie Dashboard
int menuStyle = 0; // 0 = List, 1 = Animated

// Stany aplikacji
enum AppState { STATE_FUNCTION, STATE_MAIN_MENU, STATE_SETTINGS_MENU, STATE_STARTSCREEN_MENU };
AppState appState = STATE_MAIN_MENU;

// --- STEROWANIE ---
const int BUTTON_PIN = D2;
const int LONG_PRESS_TIME = 200; // ms przytrzymania dla "ENTER" (zwiększono dla stabilności)

// Zmienne przycisku
int lastButtonState = HIGH;
unsigned long buttonPressStartTime = 0;
bool buttonActive = false;
bool longPressHandled = false;
bool uiDirty = true; // Flaga odświeżania ekranu
int lastDrawnFrame = -1;

// Zmienne dla Animated Menu
int frame = 0;
int targetFrame = 0;

// Zmienne dla Animation Feature
int currentAnimationSubIndex = 0;
const int totalAnimations = 1; // Liczba dostępnych animacji (zmień jeśli dodasz więcej)

// Deklaracje funkcji
void drawLogoGraphic();
void drawBocchi();
void drawAnimation();
void drawListMenu(const char* items[], int count, int selectedIndex, const unsigned char** icons = NULL);
void drawAnimatedMenu();
void drawFuelConsumption();
void drawDashboard();
// void drawScreenVacuum(); // Wyłączone na rzecz menu
// void drawScreenTrims();  // Wyłączone na rzecz menu

void setup() {
  Serial.begin(115200); // Debug USB
 
  BTSerial.begin(38400);

  u8g2.begin();
  Wire.setClock(400000); // Przyspieszenie I2C do 400kHz dla płynniejszej animacji
  u8g2.setDrawColor(1); // draw color white
  u8g2.setBitmapMode(1); // transparent bitmaps
  u8g2.setFontMode(1); // activate transparent font mode
 
  // Konfiguracja przycisku
  pinMode(BUTTON_PIN, INPUT_PULLUP);
 
  // Inicjalizacja EEPROM (dla RP2040 emulacja w Flash)
  EEPROM.begin(64);
  savedStartScreen = EEPROM.read(0);
  if (savedStartScreen > 4) savedStartScreen = 4; // Zabezpieczenie (0-4 to poprawne indeksy)
  menuStyle = EEPROM.read(1); // Odczyt stylu menu (0 lub 1)

  // Odczyt ostatnio wybranej animacji
  currentAnimationSubIndex = EEPROM.read(2);
  if (currentAnimationSubIndex >= totalAnimations) currentAnimationSubIndex = 0;

  // Rysuj logo na start
  drawLogoGraphic();
  logoStartTime = millis();

  // Próba połączenia z protokołem '5' (KWP2000 Fast Init - Hyundai)
  if (myELM327.begin(BTSerial, false, 2000, '5')) { // Debug false, timeout 2s
    connected = true;
    Serial.println("Polaczono z ECU!");
  } else {
    connected = false;
    Serial.println("Brak polaczenia w setupie...");
  }

  // Automatyczne wejście do zapisanego ekranu po starcie
  appState = STATE_FUNCTION;
  currentMenuIndex = savedStartScreen;
  targetFrame = currentMenuIndex * 25; // Ustawienie początkowej klatki dla animowanego menu (150/6 = 25)
  frame = targetFrame; // Zapobiegaj przewijaniu przy pierwszym wejściu do menu
}

void loop() {
  // 1. OBSŁUGA CZASU LOGO
  if (millis() - logoStartTime < LOGO_DURATION) {
    return;
  }

  // 2. OBSŁUGA PRZYCISKU (D2)
  int reading = digitalRead(BUTTON_PIN);

  // Debouncing (eliminacja drgań styków i zakłóceń)
  static unsigned long lastDebounceTime = 0;
  static int debouncedState = HIGH;

  if (reading != lastButtonState) {
    lastDebounceTime = millis();
  }

  if ((millis() - lastDebounceTime) > 50) { // 50ms opóźnienia dla stabilizacji
    if (reading != debouncedState) {
      debouncedState = reading;

      // Zmiana stanu na WCIŚNIĘTY (LOW)
      if (debouncedState == LOW) {
        buttonPressStartTime = millis();
        buttonActive = true;
        longPressHandled = false;
      }
      
      // Zmiana stanu na PUSZCZONY (HIGH)
      if (debouncedState == HIGH) {
        if (buttonActive && !longPressHandled) {
          // AKCJA KRÓTKIEGO KLIKNIĘCIA
          if (appState == STATE_MAIN_MENU) {
            currentMenuIndex++;
            if (currentMenuIndex >= menuCount) currentMenuIndex = 0;
            targetFrame = currentMenuIndex * 25; // Aktualizacja celu animacji
          } 
          else if (appState == STATE_SETTINGS_MENU) {
            settingsMenuIndex++;
            if (settingsMenuIndex >= settingsCount) settingsMenuIndex = 0;
          }
          else if (appState == STATE_STARTSCREEN_MENU) {
            startScreenMenuIndex++;
            if (startScreenMenuIndex >= 5) startScreenMenuIndex = 0; // Tylko 5 pierwszych to aplikacje
          }
          else if (appState == STATE_FUNCTION) {
             // Obsługa kliknięcia wewnątrz funkcji
             if (currentMenuIndex == 0) { // Jeśli jesteśmy w "Animation"
                currentAnimationSubIndex++;
                if (currentAnimationSubIndex >= totalAnimations) currentAnimationSubIndex = 0;
                
                // Zapisz wybór animacji do EEPROM
                EEPROM.write(2, currentAnimationSubIndex);
                #ifdef ARDUINO_ARCH_RP2040
                EEPROM.commit();
                #endif
             }
          }
        }
        buttonActive = false;
      }
    }
  }

  // Obsługa przytrzymania (Long Press) - sprawdzamy stabilny stan
  if (debouncedState == LOW && buttonActive && !longPressHandled) {
    if (millis() - buttonPressStartTime > LONG_PRESS_TIME) {
      longPressHandled = true;
      // AKCJA DŁUGIEGO NACIŚNIĘCIA
      if (appState == STATE_MAIN_MENU) {
        if (currentMenuIndex == 5) { // Settings
          appState = STATE_SETTINGS_MENU;
          settingsMenuIndex = 0;
        } else {
          appState = STATE_FUNCTION; // Uruchom wybraną aplikację
          uiDirty = true;
        }
      } 
      else if (appState == STATE_FUNCTION) {
        appState = STATE_MAIN_MENU; // Wyjście z aplikacji do menu
      }
      else if (appState == STATE_SETTINGS_MENU) {
        if (settingsMenuIndex == 0) { // Start Screen
          appState = STATE_STARTSCREEN_MENU;
          startScreenMenuIndex = savedStartScreen; // Ustaw kursor na aktualnym ustawieniu
        } 
        else if (settingsMenuIndex == 1) { // Menu Style Toggle
          menuStyle = !menuStyle; // Przełącz 0 <-> 1
          
          // Synchronizacja klatki, aby uniknąć przewijania po zmianie stylu
          if (menuStyle == 1) {
             targetFrame = currentMenuIndex * 25;
             frame = targetFrame;
          }

          EEPROM.write(1, menuStyle);
          #ifdef ARDUINO_ARCH_RP2040
          EEPROM.commit();
          #endif
        }
        else { // Back
          appState = STATE_MAIN_MENU;
        }
      }
      else if (appState == STATE_STARTSCREEN_MENU) {
        // Zapisz wybór i wróć
        savedStartScreen = startScreenMenuIndex;
        EEPROM.write(0, savedStartScreen);
        #ifdef ARDUINO_ARCH_RP2040
        EEPROM.commit(); // Wymagane dla RP2040
        #endif
        
        appState = STATE_SETTINGS_MENU;
      }
    }
  }

  lastButtonState = reading;


  // 3. LOGIKA WYŚWIETLANIA
  if (appState == STATE_MAIN_MENU) {
    if (menuStyle == 1) {
      drawAnimatedMenu();
    } else {
      drawListMenu(menuItems, menuCount, currentMenuIndex, menu_icons_16);
    }
  } 
  else if (appState == STATE_SETTINGS_MENU) {
    drawListMenu(settingsItems, settingsCount, settingsMenuIndex, NULL);
  }
  else if (appState == STATE_STARTSCREEN_MENU) {
    drawListMenu(menuItems, 5, startScreenMenuIndex, menu_icons_16); // Pokaż tylko 5 aplikacji
   
  } else if (appState == STATE_FUNCTION) {
    // --- TRYB FUNKCJI ---
   
    if (currentMenuIndex == 0) {
      drawAnimation();
    }
    else if (currentMenuIndex == 1) {
      drawFuelConsumption();
    }
    else if (currentMenuIndex == 4) {
      drawDashboard();
    }
    else {
      // Placeholder dla pozostałych (Park Sensor, GPS)
      u8g2.clearBuffer();
      u8g2.setFont(u8g2_font_helvB10_tr);
      int w = u8g2.getStrWidth(menuItems[currentMenuIndex]);
      u8g2.drawStr(64 - w/2, 25, menuItems[currentMenuIndex]);
     
      u8g2.setFont(u8g2_font_helvR08_tr);
      const char* info = "Not implemented yet";
      w = u8g2.getStrWidth(info);
      u8g2.drawStr(64 - w/2, 45, info);
     
      u8g2.setFont(u8g2_font_u8glib_4_tf);
      u8g2.drawStr(0, 64, "Hold to exit");
      u8g2.sendBuffer();
    }
  }
}


// --- FUNKCJA LOGO ---
void drawLogoGraphic() {
  u8g2.clearBuffer();
  // Rysuje bitmapę na całym ekranie
  u8g2.drawXBMP(0, 0, LOGO_WIDTH, LOGO_HEIGHT, epd_bitmap_hyundai_logo);
  u8g2.sendBuffer();
}

// --- FUNKCJA MENU LISTY ---
void drawListMenu(const char* items[], int count, int selectedIndex, const unsigned char** icons) {
  u8g2.clearBuffer();
  u8g2.setBitmapMode(1); // Ensure transparent mode
  
  // Obliczanie indeksów poprzedniego i następnego elementu
  int prevIndex = selectedIndex - 1;
  if (prevIndex < 0) prevIndex = count - 1;
  int nextIndex = selectedIndex + 1;
  if (nextIndex >= count) nextIndex = 0;

  // Rysowanie tła wybranego elementu (ramka)
  u8g2.drawXBMP(0, 22, 128, 21, bitmap_item_sel_outline);

  // Funkcja pomocnicza do rysowania pojedynczego elementu
  auto drawItem = [&](int idx, int y, bool selected) {
    if (selected) u8g2.setFont(u8g2_font_7x14B_tf); // Pogrubiona dla wybranego
    else u8g2.setFont(u8g2_font_7x14_tf);           // Zwykła dla reszty
    
    int xText = 25;
    if (icons == NULL) xText = 5; // Mniejszy margines, jeśli nie ma ikon

    u8g2.drawStr(xText, y, items[idx]);
    
    if (icons != NULL) {
       u8g2.drawXBMP(4, y - 13, 16, 16, icons[idx]);
    }
    
    // Dodatkowe info dla opcji "Menu Style" w Settings
    if (strcmp(items[idx], "Menu Style") == 0) {
         u8g2.drawStr(90, y, (menuStyle == 1) ? "[Anim]" : "[List]");
    }
  };

  // Rysowanie elementów (Poprzedni, Wybrany, Następny)
  // Y coordinates: 15, 37 (15+22), 59 (37+22) - dopasowane do czcionki i ramki
  drawItem(prevIndex, 15, false);
  drawItem(selectedIndex, 37, true);
  drawItem(nextIndex, 59, false);

  // Rysowanie paska przewijania
  u8g2.drawXBMP(128-8, 0, 8, 64, bitmap_scrollbar_background);
  
  if (count > 0) {
      int h = 64 / count;
      if (h < 4) h = 4; // Minimalna wysokość uchwytu
      // Obliczanie pozycji Y uchwytu
      int y = (64 * selectedIndex) / count;
      // Korekta, aby nie wyjechać poza ekran przy ostatnim elemencie
      if (y + h > 64) y = 64 - h;
      
      u8g2.drawBox(125, y, 3, h);
  }
  
  u8g2.sendBuffer();
}

// --- FUNKCJA ANIMOWANEGO MENU ---
void drawAnimatedMenu() {
  // Animacja do celu
  if (frame != targetFrame) {
    frame++;
    if (frame > 150) frame = 0;
  }

  u8g2.clearBuffer();

  // Rysowanie ikon w pętli (6 elementów)
  // 150 klatek / 6 elementów = 25 klatek odstępu
  for (int i = 0; i < 6; i++) {
    int item_frame = (frame + 150 - (25*i)) % 150;
    
    byte item_scale_frame;
    byte icon_size;
    
    if ((item_frame >= 0) && (item_frame <= 18)) {
      item_scale_frame = (item_frame / 2);
      icon_size = 32;
    } else if ((item_frame > 132) && (item_frame <= 150)) {
      item_scale_frame = (150 - item_frame) / 2;
      icon_size = 32;
    } else {
      item_scale_frame = 10;
      icon_size = 16;
    }
    
    byte xpos = menu_positions[item_frame][0];
    byte ypos = menu_positions[item_frame][1];
    u8g2.drawXBMP( xpos-icon_size/2, ypos-icon_size/2, icon_size, icon_size, bitmap_icons_array[item_scale_frame + (i * 11)]);
  }

  u8g2.setFont(u8g2_font_helvR08_tr);
  int w = u8g2.getStrWidth(menuItems[currentMenuIndex]);
  u8g2.drawStr(64 - w/2, 64, menuItems[currentMenuIndex]);
  u8g2.sendBuffer();
}

// --- FUNKCJA WYBORU ANIMACJI ---
void drawAnimation() {
  if (currentAnimationSubIndex == 0) {
    drawBocchi();
  } 
}

// --- FUNKCJA BOCCHI ANIMATION ---
int bocchiFrame = 0;
unsigned long lastBocchiTime = 0;
const int bocchiFrameInterval = 30; // ok. 33 FPS

void drawBocchi() {
  // Aktualizacja klatki tylko co określony czas
  if (millis() - lastBocchiTime > bocchiFrameInterval) {
    bocchiFrame++;
    if (bocchiFrame >= epd_bitmap_allArray_LEN) {
      bocchiFrame = 0;
    }
    lastBocchiTime = millis();
  }
  
  // Rysowanie (zawsze, aby odświeżyć ekran po przełączeniu)
  uint8_t* buf = u8g2.getBufferPtr();
  memcpy(buf, epd_bitmap_allArray[bocchiFrame], 1024); 
  u8g2.sendBuffer();
}

// --- FUNKCJA TURBO GAUGE ---
void drawFuelConsumption() {
  // Zmienne statyczne (pamięć między cyklami)
  static int measureState = 0;
  static float current_maf = 0.0;
  static int32_t current_speed = 0;
 
  // Zmienne do wyniku i wygładzania
  static float displayed_consumption = 0.0; 
  static bool is_liters_per_hour = false; // Flaga: czy pokazujemy L/h czy L/100km

  // 1. ODCZYT DANYCH (Naprzemienny, żeby nie resetować Wemosa)
  if (measureState == 0) {
    // Pobieramy MAF (Masa powietrza w g/s)
    float temp_maf = myELM327.mafRate();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      if (temp_maf >= 0) current_maf = temp_maf;
    }
    measureState = 1;
  }
  else {
    // Pobieramy Prędkość
    int32_t temp_speed = myELM327.kph();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      if (temp_speed >= 0) current_speed = temp_speed;
    }
    measureState = 0;
  }
 
  yield(); // Oddech dla procesora

  // 2. OBLICZENIA FIZYCZNE
  // Stałe dla benzyny: AFR = 14.7, Gęstość = ok. 740g/L
  // Wzór: Litry/h = (MAF * 3600) / (14.7 * 740)
  // Uproszczony współczynnik: 3600 / (14.7 * 740) ~= 0.331
 
  float liters_per_hour = current_maf * 0.331;
  float raw_consumption = 0.0;

  if (current_speed > 3) {
    // JEDZIEMY: Liczymy L/100km
    // Wzór: (L/h / km/h) * 100
    raw_consumption = (liters_per_hour / (float)current_speed) * 100.0;
    is_liters_per_hour = false;
  } else {
    // STOIMY: Pokazujemy L/h
    raw_consumption = liters_per_hour;
    is_liters_per_hour = true;
  }

  // Zabezpieczenie przed "nieskończonością" przy hamowaniu silnikiem (MAF ~0)
  if (raw_consumption < 0.1) raw_consumption = 0.0;
  // Zabezpieczenie górne dla paska (żeby nie wyszedł poza ekran przy ruszaniu)
  if (raw_consumption > 99.9) raw_consumption = 99.9;

  // Wygładzanie (EMA - Exponential Moving Average) dla efektu "stockowych zegarów"
  // 90% starej wartości + 10% nowej - eliminuje skoki cyfr
  displayed_consumption = (displayed_consumption * 0.9) + (raw_consumption * 0.1);


  // 3. RYSOWANIE EKRANU
  u8g2.clearBuffer();
 
  // Tytuł
  u8g2.setFont(u8g2_font_helvR08_tr);
  if (is_liters_per_hour) {
    u8g2.drawStr(10, 10, "Fuel Flow (Idle)");
  } else {
    u8g2.drawStr(10, 10, "Inst. Consumption");
  }

  // Główna wartość (Wielka czcionka)
  u8g2.setFont(u8g2_font_logisoso32_tn); // Same cyfry, duże
 
  // Centrowanie napisu
  // Przybliżona szerokość dla 3 znaków w tym foncie to ok 50-60px
  u8g2.setCursor(30, 50); // Uproszczone pozycjonowanie
  u8g2.print(displayed_consumption, 1);

  // Jednostka (Mała, obok)
  u8g2.setFont(u8g2_font_helvB12_tr);
  if (is_liters_per_hour) {
    u8g2.drawStr(95, 50, "L/h");
  } else {
    u8g2.setFont(u8g2_font_helvB10_tr); // Mniejsza dla dłuższego napisu
    u8g2.drawStr(95, 50, "L/100");
  }

  // Pasek wizualizacji (Ekonomizer graficzny)
  // Skala: 0 do 20 litrów
  int barWidth = map((long)(displayed_consumption * 10), 0, 200, 0, 128);
  // Jeśli stoimy (L/h), skala jest inna (0 do 5 L/h)
  if (is_liters_per_hour) {
     barWidth = map((long)(displayed_consumption * 10), 0, 50, 0, 128);
  }

  // Rysowanie ramki i paska
  u8g2.drawFrame(0, 56, 128, 8);
  if (barWidth > 128) barWidth = 128; // Przycięcie
  u8g2.drawBox(0, 56, barWidth, 8);

  // Podziałki na pasku dla lepszej czytelności (co 25%)
  u8g2.setColorIndex(0); // Rysujemy "gumką" (na czarno wewnątrz białego paska)
  u8g2.drawLine(32, 56, 32, 63);
  u8g2.drawLine(64, 56, 64, 63);
  u8g2.drawLine(96, 56, 96, 63);
  u8g2.setColorIndex(1); // Wracamy do rysowania na biało

  u8g2.sendBuffer();
  yield(); // Zapobieganie restartom
}

// --- FUNKCJA DASHBOARD (SPEED) ---
void drawDashboard() {
  // Zmienne statyczne (pamiętają stan między cyklami)
  static int measureState = 0;
  static int32_t displayed_speed = 0;
  static float displayed_rpm = 0;

  // 1. ODCZYT DANYCH (Naprzemienny: Raz prędkość, raz obroty)
  // To eliminuje błędne odczyty typu "2848 km/h"
  if (measureState == 0) {
    int32_t temp_speed = myELM327.kph();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      if (temp_speed >= 0 && temp_speed < 260) {
        displayed_speed = temp_speed;
        last_speed = temp_speed;
      }
    }
    measureState = 1; // Przełącz na RPM
  }
  else {
    float temp_rpm = myELM327.rpm();
    if (myELM327.nb_rx_state == ELM_SUCCESS) {
      if (temp_rpm >= 0 && temp_rpm < 9000) {
        displayed_rpm = temp_rpm;
      }
    }
    measureState = 0; // Przełącz na Speed
  }
 
  yield(); // Oddech dla procesora (BARDZO WAŻNE)

  // 2. LOGIKA BIEGÓW
  int currentGear = 0;
  int suggestedGear = 0;
  int arrowDir = 0;

  // Obliczamy tylko gdy auto jedzie
  if (displayed_speed > 3 && displayed_rpm > 400) {
    float ratio = displayed_rpm / (float)displayed_speed;

    // KALIBRACJA HYUNDAI COUPE
    if (ratio > 110.0) currentGear = 1;
    else if (ratio > 68.0) currentGear = 2;
    else if (ratio > 48.0) currentGear = 3;
    else if (ratio > 36.0) currentGear = 4;
    else currentGear = 5;

    // LOGIKA ECO (Twoje wytyczne)
    // 1 -> 2: Zmiana dopiero przy 3400 RPM (płynność)
    if (currentGear == 1) {
       if (displayed_rpm > 3400) {
         suggestedGear = 2;
         arrowDir = 1; // Strzałka w górę
       }
    }
    // Pozostałe biegi: Zmiana przy 3000 RPM (ekonomia)
    else if (currentGear > 1 && currentGear < 5) {
       if (displayed_rpm > 3000) {
         suggestedGear = currentGear + 1;
         arrowDir = 1;
       }
    }
   
    // Redukcja (Dławienie silnika poniżej 1200)
    if (displayed_rpm < 1200 && currentGear > 1) {
      suggestedGear = currentGear - 1;
      arrowDir = -1; // Strzałka w dół
    }
  }

  // 3. RYSOWANIE EKRANU
  u8g2.clearBuffer();
 
  // --- LEWA STRONA (Wielka Prędkość) ---
  u8g2.setFont(u8g2_font_logisoso42_tn);
  u8g2.setCursor(5, 55);
  u8g2.print(displayed_speed);
 
  // PIONOWA LINIA
  u8g2.drawLine(80, 5, 80, 59);

  // --- PRAWA STRONA (Biegi) ---
  if (currentGear > 0) {
    // Czy jest sugestia zmiany?
    if (suggestedGear > 0) {
      if (arrowDir == 1) {
        // Strzałka W GÓRĘ
        u8g2.drawTriangle(95, 45, 105, 25, 115, 45);
        u8g2.drawBox(102, 45, 6, 10);
      } else {
        // Strzałka W DÓŁ
        u8g2.drawTriangle(95, 35, 105, 55, 115, 35);
        u8g2.drawBox(102, 25, 6, 10);
      }
      // Numer sugerowanego biegu
      u8g2.setFont(u8g2_font_logisoso16_tn);
      u8g2.setCursor(120, 60);
      u8g2.print(suggestedGear);
    }
    // Brak sugestii - jedziesz optymalnie
    else {
      u8g2.setFont(u8g2_font_helvR08_tr);
      u8g2.drawStr(95, 20, "GEAR");
      u8g2.setFont(u8g2_font_helvB14_tr);
      u8g2.setCursor(98, 45);
      u8g2.print(currentGear);
    }
  }
  else {
    // Postój (Luz)
    u8g2.setFont(u8g2_font_helvB10_tr);
    u8g2.drawStr(90, 35, " - - ");
  }

  u8g2.sendBuffer();
  yield();
}