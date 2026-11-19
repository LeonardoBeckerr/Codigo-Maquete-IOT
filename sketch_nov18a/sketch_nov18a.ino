
#include <Servo.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Buzzer e servo
const int BUZZER = 8;
const int SERVO_PIN = 3;

// Sensor de abertura da tampa
const int TRIG_ABERTURA = 7;
const int ECHO_ABERTURA = 6;

// Sensor de lixeira cheia
const int TRIG_LIXEIRA_CHEIA = 2;
const int ECHO_LIXEIRA_CHEIA = 9;

// LCD I2C
LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo tampa;

// Configuracoes
const float DISTANCIA_ABRIR = 30.0;
const float DISTANCIA_CHEIA = 5.0;
const float DISTANCIA_VAZIA = 8.0;

const int TAMPA_FECHADA = 0;
const int TAMPA_ABERTA = 90;

const unsigned long TEMPO_ABERTA = 3000;

// Estados
bool tampaEstaAberta = false;
bool aguardarRetirada = false;
bool lixeiraCheia = false;

int ultimaPosicaoServo = -1;

unsigned long momentoAbertura = 0;
unsigned long ultimaAlternanciaBuzzer = 0;
unsigned long ultimaLeituraSerial = 0;

bool buzzerLigado = false;

int leiturasMaoPerto = 0;
int leiturasParaCheia = 0;
int leiturasParaVazia = 0;

// Mede distancia com HC-SR04
float medirDistancia(int trig, int echo) {

  digitalWrite(trig, LOW);
  delayMicroseconds(2);

  digitalWrite(trig, HIGH);
  delayMicroseconds(10);

  digitalWrite(trig, LOW);

  unsigned long duracao = pulseIn(echo, HIGH, 25000UL);

  if (duracao == 0) {
    return -1;
  }

  return (duracao * 0.0343) / 2.0;
}

// Movimenta o servo somente quando a posicao muda
void moverTampa(int angulo) {

  if (ultimaPosicaoServo != angulo) {

    tampa.write(angulo);

    ultimaPosicaoServo = angulo;
  }
}

// Escreve mensagem no LCD
void escreverLinha(int linha, const char* mensagem) {

  lcd.setCursor(0, linha);
  lcd.print("                ");

  lcd.setCursor(0, linha);
  lcd.print(mensagem);
}

// Atualiza o estado da lixeira com leituras consecutivas
void atualizarLixeiraCheia(float distancia) {

  // Ignora leituras invalidas
  if (distancia <= 0) {
    return;
  }

  if (!lixeiraCheia) {

    if (distancia <= DISTANCIA_CHEIA) {

      leiturasParaCheia++;

      if (leiturasParaCheia >= 3) {

        lixeiraCheia = true;

        leiturasParaCheia = 0;
        leiturasParaVazia = 0;
      }

    } else {

      leiturasParaCheia = 0;
    }

  } else {

    if (distancia >= DISTANCIA_VAZIA) {

      leiturasParaVazia++;

      if (leiturasParaVazia >= 3) {

        lixeiraCheia = false;

        leiturasParaCheia = 0;
        leiturasParaVazia = 0;
      }

    } else {

      leiturasParaVazia = 0;
    }
  }
}

void setup() {

  Serial.begin(9600);

  // Sensores
  pinMode(TRIG_ABERTURA, OUTPUT);
  pinMode(ECHO_ABERTURA, INPUT);

  pinMode(TRIG_LIXEIRA_CHEIA, OUTPUT);
  pinMode(ECHO_LIXEIRA_CHEIA, INPUT);

  digitalWrite(TRIG_ABERTURA, LOW);
  digitalWrite(TRIG_LIXEIRA_CHEIA, LOW);

  // Buzzer
  pinMode(BUZZER, OUTPUT);

  // Servo
  tampa.attach(SERVO_PIN);
  moverTampa(TAMPA_FECHADA);

  // LCD
  lcd.init();
  lcd.backlight();

  escreverLinha(0, "LIXEIRA SMART");
  escreverLinha(1, "Iniciando...");

  Serial.println("LIXEIRA INTELIGENTE");
  Serial.println("------------------------------");

  delay(2000);
}

void loop() {

  // Leitura do sensor de abertura
  float distanciaMao = medirDistancia(
    TRIG_ABERTURA,
    ECHO_ABERTURA
  );

  // Evita interferencia entre sensores ultrassonicos
  delay(60);

  // Leitura do sensor de lixeira cheia
  float distanciaNivel = medirDistancia(
    TRIG_LIXEIRA_CHEIA,
    ECHO_LIXEIRA_CHEIA
  );

  unsigned long agora = millis();

  // Atualiza a deteccao de lixeira cheia
  atualizarLixeiraCheia(distanciaNivel);

  // PRIORIDADE: lixeira cheia
  if (lixeiraCheia) {

    moverTampa(TAMPA_FECHADA);

    tampaEstaAberta = false;
    aguardarRetirada = true;

    leiturasMaoPerto = 0;

    // Alerta sonoro intermitente
    if (agora - ultimaAlternanciaBuzzer >= 500) {

      ultimaAlternanciaBuzzer = agora;

      buzzerLigado = !buzzerLigado;

      if (buzzerLigado) {
        tone(BUZZER, 1000);
      } else {
        noTone(BUZZER);
      }
    }

    escreverLinha(0, "LIXEIRA CHEIA");
    escreverLinha(1, "ESVAZIE AGORA");

  } else {

    noTone(BUZZER);
    buzzerLigado = false;

    // Fecha a tampa depois de 3 segundos
    if (tampaEstaAberta &&
        agora - momentoAbertura >= TEMPO_ABERTA) {

      moverTampa(TAMPA_FECHADA);

      tampaEstaAberta = false;
      aguardarRetirada = true;

      leiturasMaoPerto = 0;
    }

    // Aguarda a retirada da mao
    if (aguardarRetirada) {

      if (distanciaMao <= 0 ||
          distanciaMao > DISTANCIA_ABRIR + 5) {

        aguardarRetirada = false;
      }

      leiturasMaoPerto = 0;
    }

    // Detecta a aproximacao da mao
    else if (!tampaEstaAberta) {

      if (distanciaMao > 0 &&
          distanciaMao <= DISTANCIA_ABRIR) {

        leiturasMaoPerto++;

        // Exige duas deteccoes consecutivas
        if (leiturasMaoPerto >= 2) {

          moverTampa(TAMPA_ABERTA);

          tampaEstaAberta = true;
          momentoAbertura = agora;

          leiturasMaoPerto = 0;
        }

      } else {

        leiturasMaoPerto = 0;
      }
    }

    // Atualiza o LCD
    if (tampaEstaAberta) {

      escreverLinha(0, "TAMPA ABERTA");
      escreverLinha(1, "PODE DESCARTAR");

    } else {

      escreverLinha(0, "LIXEIRA PRONTA");
      escreverLinha(1, "APROXIME A MAO");
    }
  }

  // Monitoramento no Monitor Serial
  if (agora - ultimaLeituraSerial >= 300) {

    ultimaLeituraSerial = agora;

    Serial.print("Sensor abertura: ");
    Serial.print(distanciaMao);

    Serial.print(" cm | Sensor lixo cheio: ");
    Serial.print(distanciaNivel);

    Serial.print(" cm | Estado: ");

    if (lixeiraCheia) {
      Serial.println("CHEIA");
    } else {
      Serial.println("NORMAL");
    }
  }

  delay(30);
}
