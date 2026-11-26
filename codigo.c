#include <Servo.h>             // Controle de servomotores
#include <Wire.h>              // Comunicacao I2C (usada pelo display LCD)
#include <LiquidCrystal_I2C.h> // Controle de display LCD 16x2 via I2C

// --- Pinos de atuadores ---
const int BUZZER = 2;    // Pino digital ligado ao buzzer de alerta
const int SERVO_PIN = 3; // Pino PWM ligado ao servo da tampa

// --- Pinos do sensor ultrassonico de abertura (mao) ---
const int TRIG_ABERTURA = 4; // Pulso de disparo do sensor de abertura
const int ECHO_ABERTURA = 5; // Retorno do eco do sensor de abertura

// --- Pinos do sensor ultrassonico de nivel (lixeira cheia) ---
const int TRIG_LIXEIRA_CHEIA = 6; // Pulso de disparo do sensor interno
const int ECHO_LIXEIRA_CHEIA = 7; // Retorno do eco do sensor interno

// Endereco 0x27 no barramento I2C, display de 16 colunas por 2 linhas
LiquidCrystal_I2C lcd(0x27, 16, 2);

Servo tampa; // Objeto que representa o servomotor da tampa

// --- Parametros de distancia (em cm) ---
const float DISTANCIA_ABRIR = 30.0; // Mao ate 30 cm aciona a abertura
const float DISTANCIA_CHEIA = 6.0;  // Lixo a 6 cm ou menos do sensor = cheia
const float DISTANCIA_VAZIA = 8.0;  // Histerese: so volta a "nao cheia" a partir de 8 cm (evita oscilacao)

// --- Angulos da tampa no servo ---
const int TAMPA_FECHADA = 0; // Posicao fechada (ajuste conforme montagem mecanica)
const int TAMPA_ABERTA = 90; // Posicao aberta

// --- Temporizadores (em milissegundos) ---
const unsigned long TEMPO_ABERTA = 3000;          // Tempo que a tampa fica aberta apos abrir
const unsigned long TEMPO_ESTABILIZACAO = 2000; // Pausa apos fechar para o lixo/tampa assentarem antes de medir o nivel

// --- Flags de estado do sistema ---
bool tampaEstaAberta = false;          // Indica se a tampa esta aberta no momento
bool aguardarRetirada = false;         // Evita reabrir sozinha enquanto a mao ainda esta na frente
bool lixeiraCheia = false;             // Indica se a lixeira atingiu o nivel maximo
bool verificacaoNivelLiberada = true;  // Controla quando e seguro ler o sensor de nivel

int ultimaPosicaoServo = -1; // Guarda o ultimo angulo enviado (-1 forca o primeiro movimento)

// --- Controle de tempo sem travar o loop (millis) ---
unsigned long momentoAbertura = 0;         // Instante em que a tampa abriu
unsigned long momentoFechamento = 0;       // Instante em que a tampa comecou a fechar
unsigned long ultimaAlternanciaBuzzer = 0; // Ultima troca de estado (ligado/desligado) do buzzer

bool buzzerLigado = false; // Estado atual do bip intermitente

// --- Contadores de confirmacao (filtro antirruido) ---
// Exigem multiplas leituras seguidas para evitar falsos positivos/negativos
int leiturasMaoPerto = 0;
int leiturasParaCheia = 0;
int leiturasParaVazia = 0;

// Mede a distancia em centimetros usando um sensor HC-SR04
float medirDistancia(int trig, int echo) {
  // Garante nivel baixo antes de enviar o pulso
  digitalWrite(trig, LOW);
  delayMicroseconds(2);

  // Envia pulso ultrassonico de 10 microssegundos
  digitalWrite(trig, HIGH);
  delayMicroseconds(10);
  digitalWrite(trig, LOW);

  // Espera o pino ECHO ir a HIGH e mede a duracao (timeout de 25 ms = ~4,3 m)
  unsigned long duracao = pulseIn(echo, HIGH, 25000UL);

  // Se nao recebeu eco dentro do tempo limite, retorna -1 como erro de leitura
  if (duracao == 0) {
    return -1;
  }

  // Velocidade do som ~0,0343 cm/us; divide por 2 porque o som vai e volta
  return (duracao * 0.0343) / 2.0;
}

// Move o servo apenas se o angulo solicitado for diferente do atual (evita trepidacao)
void moverTampa(int angulo) {
  if (ultimaPosicaoServo != angulo) {
    tampa.write(angulo);
    ultimaPosicaoServo = angulo;
  }
}

// Limpa e escreve uma linha inteira (16 caracteres) no LCD sem deixar restos de textos anteriores
void escreverLinha(int linha, const char* mensagem) {
  lcd.setCursor(0, linha);
  lcd.print("                "); // 16 espacos em branco

  lcd.setCursor(0, linha);
  lcd.print(mensagem);
}

// Atualiza o estado de "lixeira cheia" com filtro de 3 leituras e histerese
void atualizarLixeiraCheia(float distancia) {
  // Ignora leituras invalidas do sensor
  if (distancia <= 0) {
    return;
  }

  if (!lixeiraCheia) {
    // Quando nao esta cheia, conta leituras abaixo ou iguais a DISTANCIA_CHEIA
    if (distancia <= DISTANCIA_CHEIA) {
      leiturasParaCheia++;

      if (leiturasParaCheia >= 3) {
        lixeiraCheia = true;
        leiturasParaCheia = 0;
        leiturasParaVazia = 0;
      }
    } else {
      leiturasParaCheia = 0; // Reinicia se uma leitura sair da faixa
    }
  } else {
    // Quando ja esta cheia, exige distancia >= DISTANCIA_VAZIA para considerar esvaziada
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
  Serial.begin(9600); // Inicia comunicacao serial para depuracao no PC

  // Configura direcao dos pinos dos sensores ultrassonicos
  pinMode(TRIG_ABERTURA, OUTPUT);
  pinMode(ECHO_ABERTURA, INPUT);

  pinMode(TRIG_LIXEIRA_CHEIA, OUTPUT);
  pinMode(ECHO_LIXEIRA_CHEIA, INPUT);

  // Garante disparos em nivel baixo na inicializacao
  digitalWrite(TRIG_ABERTURA, LOW);
  digitalWrite(TRIG_LIXEIRA_CHEIA, LOW);

  // Configura pino do buzzer como saida
  pinMode(BUZZER, OUTPUT);

  // Anexa o servo ao pino e inicia com a tampa fechada
  tampa.attach(SERVO_PIN);
  moverTampa(TAMPA_FECHADA);

  // Inicializa o display LCD e liga a luz de fundo
  lcd.init();
  lcd.backlight();

  escreverLinha(0, "LIXEIRA SMART");
  escreverLinha(1, "Iniciando...");

  delay(2000); // Pausa inicial para exibir mensagem de boas-vindas
}

void loop() {
  unsigned long agora = millis(); // Tempo atual desde que o Arduino ligou

  // 1. Le o sensor externo para detectar mao/objeto proximo
  float distanciaMao = medirDistancia(TRIG_ABERTURA, ECHO_ABERTURA);

  delay(60); // Pequeno intervalo para evitar interferencia acustica entre leituras

  // 2. Fecha a tampa automaticamente apos TEMPO_ABERTA
  if (tampaEstaAberta) {
    if (agora - momentoAbertura >= TEMPO_ABERTA) {
      moverTampa(TAMPA_FECHADA);

      tampaEstaAberta = false;
      aguardarRetirada = true; // Exige tirar a mao antes de abrir novamente

      momentoFechamento = agora; // Marca inicio da contagem de estabilizacao

      // Zera contadores de nivel para comecar uma avaliacao limpa apos fechar
      leiturasParaCheia = 0;
      leiturasParaVazia = 0;
    }
  }

  // 3. Aguarda a mao sair da zona de deteccao antes de permitir nova abertura
  if (aguardarRetirada) {
    // Libera quando nao ha leitura valida ou a mao esta alem da distancia de abertura (+5 cm de margem)
    if (distanciaMao <= 0 || distanciaMao > DISTANCIA_ABRIR + 5) {
      aguardarRetirada = false;
    }

    leiturasMaoPerto = 0;
  }
  // 4. Se a tampa esta fechada e a lixeira nao esta cheia, verifica se ha mao para abrir
  else if (!tampaEstaAberta && !lixeiraCheia) {
    if (distanciaMao > 0 && distanciaMao <= DISTANCIA_ABRIR) {
      leiturasMaoPerto++;

      // Abre somente apos 2 leituras consecutivas validas (evita acionamento acidental)
      if (leiturasMaoPerto >= 2) {
        moverTampa(TAMPA_ABERTA);

        tampaEstaAberta = true;
        momentoAbertura = agora;

        leiturasMaoPerto = 0;

        // Pausa a medicao de nivel enquanto a tampa esta aberta/em movimento
        verificacaoNivelLiberada = false;

        leiturasParaCheia = 0;
        leiturasParaVazia = 0;
      }
    } else {
      leiturasMaoPerto = 0;
    }
  }

  // 5. Libera a leitura do sensor interno apos o tempo de estabilizacao pos-fechamento
  if (!tampaEstaAberta &&
      !aguardarRetirada &&
      !verificacaoNivelLiberada &&
      agora - momentoFechamento >= TEMPO_ESTABILIZACAO) {
    verificacaoNivelLiberada = true;
  }

  // 6. Mede o nivel interno apenas quando a tampa esta fechada e a leitura esta liberada
  float distanciaNivel = -1;

  if (!tampaEstaAberta && verificacaoNivelLiberada) {
    delay(30); // Pequena pausa antes de acionar o segundo sensor

    distanciaNivel = medirDistancia(TRIG_LIXEIRA_CHEIA, ECHO_LIXEIRA_CHEIA);

    atualizarLixeiraCheia(distanciaNivel);
  }

  // 7. Aplica prioridade maxima quando a lixeira esta cheia: fecha tampa, toca alerta e avisa no LCD
  if (lixeiraCheia) {
    moverTampa(TAMPA_FECHADA);

    tampaEstaAberta = false;
    aguardarRetirada = true;

    leiturasMaoPerto = 0;

    // Alterna o estado do buzzer a cada 500 ms (bip intermitente)
    if (agora - ultimaAlternanciaBuzzer >= 500) {
      ultimaAlternanciaBuzzer = agora;
      buzzerLigado = !buzzerLigado;

      if (buzzerLigado) {
        tone(BUZZER, 2500); // Buzzer PASSIVO: gera tom de 2500 Hz
        // digitalWrite(BUZZER, HIGH); // Buzzer ATIVO: use esta linha no lugar de tone()
      } else {
        noTone(BUZZER); // Para o som no buzzer passivo
        // digitalWrite(BUZZER, LOW);  // Buzzer ATIVO: use esta linha no lugar de noTone()
      }
    }

    escreverLinha(0, "LIXEIRA CHEIA");
    escreverLinha(1, "ESVAZIE AGORA");
  }
  // 8. Operacao normal: garante buzzer desligado e atualiza LCD conforme o estado atual
  else {
    noTone(BUZZER);
    // digitalWrite(BUZZER, LOW); // Descomente se usar buzzer ATIVO
    buzzerLigado = false;

    if (tampaEstaAberta) {
      escreverLinha(0, "TAMPA ABERTA");
      escreverLinha(1, "PODE DESCARTAR");
    } else if (!verificacaoNivelLiberada) {
      escreverLinha(0, "AGUARDE...");
      escreverLinha(1, "ESTABILIZANDO");
    } else {
      escreverLinha(0, "LIXEIRA PRONTA");
      escreverLinha(1, "APROXIME A MAO");
    }
  }

  // 9. Envia telemetria/status para o Serial Monitor
  Serial.print("Sensor abertura: ");
  Serial.print(distanciaMao);

  Serial.print(" cm | Sensor lixo cheio: ");
  Serial.print(distanciaNivel);

  Serial.print(" cm | Estado: ");

  if (lixeiraCheia) {
    Serial.println("CHEIA");
  } else if (tampaEstaAberta) {
    Serial.println("TAMPA ABERTA");
  } else if (!verificacaoNivelLiberada) {
    Serial.println("AGUARDANDO ESTABILIZACAO");
  } else {
    Serial.println("NORMAL");
  }

  delay(30); // Pequeno respiro no fim do ciclo
}