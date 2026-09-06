#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Keypad.h>

// =====================
// DISPLAY OLED
// =====================
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define OLED_RESET -1
Adafruit_SSD1306 display(SCREEN_WIDTH, SCREEN_HEIGHT, &Wire, OLED_RESET);

// =====================
// TECLADO 4x4
// =====================
const byte ROWS = 4;
const byte COLS = 4;
char keys[ROWS][COLS] = {
  {'1','2','3','A'},
  {'4','5','6','B'},
  {'7','8','9','C'},
  {'*','0','#','D'}
};
byte rowPins[ROWS] = {13, 12, 14, 27};
byte colPins[COLS] = {26, 25, 33, 32};
Keypad keypad = Keypad(makeKeymap(keys), rowPins, colPins, ROWS, COLS);

// =====================
// BEACONS PRÉ-CADASTRADOS
// ← Mude os nomes aqui para bater com o código de cada Beacon!
// =====================
const int TOTAL_BEACONS = 4;
String beacons[TOTAL_BEACONS] = {
  "BEACON_01",
  "BEACON_02",
  "BEACON_03",
  "BEACON_04"
};
int beaconAtivo = 0;

// =====================
// BLE
// =====================
#define SCAN_TIME 1
BLEScan* pBLEScan;
int rssiAtual = -999;
bool encontrado = false;

// =====================
// ESTADOS DO MENU
// =====================
#define TELA_SPLASH 0
#define TELA_MENU   1
#define TELA_LISTA  2
#define TELA_SINAL  3
#define TELA_INFO   4

int telaAtual = TELA_SPLASH;
int menuSel   = 0;
int listaSel  = 0;
int frameCount = 0;

// =====================
// FUNÇÕES AUXILIARES RSSI
// =====================
int rssiParaBarras(int rssi) {
  if (rssi >= -50) return 5;
  else if (rssi >= -60) return 4;
  else if (rssi >= -70) return 3;
  else if (rssi >= -80) return 2;
  else if (rssi >= -90) return 1;
  else return 0;
}

String rssiParaStatus(int rssi) {
  if (rssi >= -50) return "MT PERTO!";
  else if (rssi >= -65) return "PERTO";
  else if (rssi >= -75) return "MEDIO";
  else if (rssi >= -85) return "LONGE";
  else return "MT LONGE";
}

String rssiParaDistancia(int rssi) {
  if (rssi >= -50) return "<1m";
  else if (rssi >= -60) return "~1m";
  else if (rssi >= -70) return "~2m";
  else if (rssi >= -80) return "~5m";
  else return ">10m";
}

// =====================
// FUNÇÕES DO DISPLAY
// =====================
void drawHeader(String titulo) {
  display.fillRect(0, 0, 128, 10, SSD1306_WHITE);
  display.setTextColor(SSD1306_BLACK);
  display.setTextSize(1);
  display.setCursor(2, 2);
  display.print(titulo);
  display.setTextColor(SSD1306_WHITE);
}

void drawBarras(int barras) {
  int largura  = 16;
  int espaco   = 4;
  int totalW   = TOTAL_BEACONS * (largura + espaco) + (largura + espaco);
  int startX   = (128 - (5 * (largura + espaco) - espaco)) / 2;
  int baseY    = 62;

  for (int i = 0; i < 5; i++) {
    int altura = 8 + i * 6;
    int x = startX + i * (largura + espaco);
    int y = baseY - altura;

    if (i < barras) {
      display.fillRect(x, y, largura, altura, SSD1306_WHITE);
    } else {
      display.drawRect(x, y, largura, altura, SSD1306_WHITE);
    }
  }
}

// =====================
// TELAS
// =====================
void telaSplash() {
  display.clearDisplay();
  display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
  display.drawRect(2, 2, 124, 60, SSD1306_WHITE);

  display.setTextSize(2);
  display.setCursor(18, 8);
  display.println("RADAR");
  display.setCursor(30, 28);
  display.println("BLE");

  display.setTextSize(1);
  if (frameCount % 20 < 10) {
    display.setCursor(8, 50);
    display.println("PRESSIONE # INICIAR");
  }
  display.display();
}

void telaMenu() {
  String itens[] = {
    "1.INICIAR RADAR",
    "2.BEACONS",
    "3.INFO",
    "4.SAIR"
  };
  int total = 4;

  display.clearDisplay();
  drawHeader("MENU PRINCIPAL");

  for (int i = 0; i < total; i++) {
    int y = 13 + i * 13;
    if (i == menuSel) {
      display.fillRect(0, y - 1, 128, 11, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
    } else {
      display.setTextColor(SSD1306_WHITE);
    }
    display.setCursor(4, y);
    display.println(itens[i]);
  }
  display.setTextColor(SSD1306_WHITE);
  display.display();
}

void telaLista() {
  display.clearDisplay();
  drawHeader("ESCOLHA BEACON");

  int visiveis = 3;
  int inicio = max(0, min(listaSel, TOTAL_BEACONS - visiveis));

  for (int i = 0; i < min(visiveis, TOTAL_BEACONS); i++) {
    int idx  = inicio + i;
    int y    = 13 + i * 17;
    bool ativo = (idx == beaconAtivo);
    bool sel   = (idx == listaSel);
    String nome = (ativo ? "*" : " ") + beacons[idx];

    if (sel) {
      display.fillRect(0, y - 1, 128, 15, SSD1306_WHITE);
      display.setTextColor(SSD1306_BLACK);
      display.setCursor(2, y);
      display.println(nome);
      display.setCursor(2, y + 7);
      display.println(ativo ? "ATIVO  A=rastrear" : "A=rastrear");
    } else {
      display.setTextColor(SSD1306_WHITE);
      display.setCursor(2, y);
      display.println(nome);
    }
  }

  display.setTextColor(SSD1306_WHITE);
  if (TOTAL_BEACONS > visiveis) {
    if (listaSel > 0)                   { display.setCursor(120, 14); display.println("^"); }
    if (listaSel < TOTAL_BEACONS - 1)  { display.setCursor(120, 48); display.println("v"); }
  }
  display.display();
}

void telaSinal() {
  // Scan BLE
  encontrado = false;
  pBLEScan->start(SCAN_TIME, false);
  pBLEScan->clearResults();

  display.clearDisplay();
  drawHeader("LOCALIZADOR BLE");

  if (encontrado) {
    int barras = rssiParaBarras(rssiAtual);

    // Nome do beacon
    display.setTextSize(1);
    display.setCursor(0, 12);
    display.println(beacons[beaconAtivo]);
    display.drawLine(0, 20, 128, 20, SSD1306_WHITE);

    // Status
    display.setCursor(2, 23);
    display.println(rssiParaStatus(rssiAtual));
    display.drawLine(0, 31, 128, 31, SSD1306_WHITE);

    // dBm
    display.setCursor(0, 34);
    display.print("Sinal: ");
    display.print(rssiAtual);
    display.println(" dBm");

    // Distância
    display.setCursor(0, 43);
    display.print("Dist:  ");
    display.println(rssiParaDistancia(rssiAtual));

    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);

    // Barras
    drawBarras(barras);

    Serial.print("RSSI: ");
    Serial.print(rssiAtual);
    Serial.print(" | Status: ");
    Serial.print(rssiParaStatus(rssiAtual));
    Serial.print(" | Dist: ");
    Serial.println(rssiParaDistancia(rssiAtual));

  } else {
    display.setTextSize(1);
    display.setCursor(0, 14);
    display.println("Procurando:");
    display.setCursor(0, 24);
    display.println(beacons[beaconAtivo]);
    display.setCursor(0, 40);
    display.println("Aguardando sinal...");
    display.drawLine(0, 52, 128, 52, SSD1306_WHITE);
    drawBarras(0);
    Serial.println("Procurando beacon: " + beacons[beaconAtivo]);
  }

  display.display();
}

void telaInfo() {
  display.clearDisplay();
  drawHeader("INFORMACOES");

  display.setTextSize(1);
  display.setCursor(0, 13);
  display.println("RASTREANDO:");
  display.setCursor(0, 22);
  display.println(beacons[beaconAtivo]);
  display.drawLine(0, 31, 128, 31, SSD1306_WHITE);
  display.setCursor(0, 34);
  display.print("TOTAL BEACONS: ");
  display.println(TOTAL_BEACONS);
  display.drawLine(0, 43, 128, 43, SSD1306_WHITE);
  display.setCursor(0, 46);
  display.println("2=sobe  5=desce");
  display.setCursor(0, 55);
  display.println("B=voltar");
  display.display();
}

// =====================
// CALLBACK BLE
// =====================
class MyCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice advertisedDevice) {
    if (advertisedDevice.getName() == beacons[beaconAtivo].c_str()) {
      rssiAtual = advertisedDevice.getRSSI();
      encontrado = true;
    }
  }
};

// =====================
// SETUP
// =====================
void setup() {
  Serial.begin(115200);

  if (!display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) {
    Serial.println("Erro no display!");
    while (true);
  }
  display.setTextColor(SSD1306_WHITE);
  display.clearDisplay();
  display.setTextSize(1);
  display.setCursor(0, 0);
  display.println("Iniciando...");
  display.display();
  delay(1000);

  BLEDevice::init("ESP32_SCANNER");
  pBLEScan = BLEDevice::getScan();
  pBLEScan->setAdvertisedDeviceCallbacks(new MyCallbacks());
  pBLEScan->setActiveScan(true);
  pBLEScan->setInterval(100);
  pBLEScan->setWindow(99);

  Serial.println("Scanner pronto!");
}

// =====================
// LOOP
// =====================
void loop() {
  char key = keypad.getKey();
  frameCount++;

  // SPLASH
  if (telaAtual == TELA_SPLASH) {
    telaSplash();
    if (key == '#') telaAtual = TELA_MENU;
  }

  // MENU
  else if (telaAtual == TELA_MENU) {
    telaMenu();
    if      (key == '2') menuSel = max(0, menuSel - 1);
    else if (key == '5') menuSel = min(3, menuSel + 1);
    else if (key == 'A' || key == '#') {
      if      (menuSel == 0) telaAtual = TELA_SINAL;
      else if (menuSel == 1) { listaSel = beaconAtivo; telaAtual = TELA_LISTA; }
      else if (menuSel == 2) telaAtual = TELA_INFO;
      else if (menuSel == 3) telaAtual = TELA_SPLASH;
    }
  }

  // LISTA DE BEACONS
  else if (telaAtual == TELA_LISTA) {
    telaLista();
    if      (key == 'B')               telaAtual = TELA_MENU;
    else if (key == '2')               listaSel = max(0, listaSel - 1);
    else if (key == '5')               listaSel = min(TOTAL_BEACONS - 1, listaSel + 1);
    else if (key == 'A' || key == '#') { beaconAtivo = listaSel; telaAtual = TELA_SINAL; }
  }

  // SINAL / BARRAS
  else if (telaAtual == TELA_SINAL) {
    telaSinal();
    if (key == 'B' || key == '*') telaAtual = TELA_MENU;
  }

  // INFO
  else if (telaAtual == TELA_INFO) {
    telaInfo();
    if (key == 'B') telaAtual = TELA_MENU;
  }

  delay(50);
}