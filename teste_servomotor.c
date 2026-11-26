#include <Servo.h>


const int SERVO_PIN = 3;
Servo tampa;


void setup() {
  Serial.begin(9600);
  tampa.attach(SERVO_PIN);
  Serial.println("Iniciando teste do servo no pino 3...");
}


void loop() {
  Serial.println("Movendo para 0 graus (Fechada)");
  tampa.write(0);
  delay(2000);


  Serial.println("Movendo para 90 graus (Aberta)");
  tampa.write(90);
  delay(2000);
}