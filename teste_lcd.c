#include <Wire.h>
#include <LiquidCrystal_I2C.h>


void setup() {
  Serial.begin(9600);
  Wire.begin();
  
  Serial.println("\nProcurando dispositivo I2C...");
  byte enderecoEncontrado = 0;


  // Varre todos os enderecos possiveis (1 a 126)
  for (byte endereco = 1; endereco < 127; endereco++) {
    Wire.beginTransmission(endereco);
    byte erro = Wire.endTransmission();


    if (erro == 0) {
      Serial.print("LCD encontrado no endereco: 0x");
      if (endereco < 16) Serial.print("0");
      Serial.println(endereco, HEX);
      enderecoEncontrado = endereco;
      break;
    }
  }


  if (enderecoEncontrado == 0) {
    Serial.println("ERRO: Nenhum LCD encontrado! Verifique os fios SDA e SCL.");
  } else {
    // Inicializa o LCD com o endereco descoberto
    LiquidCrystal_I2C lcd(enderecoEncontrado, 16, 2);
    lcd.init();
    lcd.backlight();
    
    lcd.setCursor(0, 0);
    lcd.print("TESTE DE LCD OK!");
    lcd.setCursor(0, 1);
    lcd.print("Endereco: 0x");
    lcd.print(enderecoEncontrado, HEX);
  }
}


void loop() {
  // Nao precisa repetir nada no loop
}