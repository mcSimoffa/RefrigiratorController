#include <TimerOne.h>
#include <EEPROM.h>
#include <MsTimer2.h>
#define tawTime 23040000  //цикл работы компрессора до следующей оттайки (64 часов)
#define CDoorClose 720000  // время непрерывно закрытой двери, требеумое для включения оттайки (2 часа)
#define Hys 1536  //Гистерезис терморегулятора
#define EndTawTemp 95000  //показания датчика МК считаемые как "оттайка выполнена"
#define RecoverTemp 123000  //показания датчика МК считаемые как набор холода после цикла оттайки выполнен
//INPUT
#define button 2
#define DoorSensor 3
//OUTPUT
#define Motor 4
#define Heat 5
#define Lamp 6
#define LD0 7
#define LD1 8
#define LD2 9
#define Buzzer 10
#define ledPin 13     // apparar LED
volatile unsigned long TempH;
volatile unsigned long TempM;
volatile unsigned long TempH_1;
volatile unsigned long TempM_1;
volatile unsigned long AverH;
volatile unsigned long AverM;
unsigned long Ustavka;
volatile unsigned long CloseDoorTime; //счетчик времени закрытого состояния двери
volatile unsigned long TimeBase;      //таймер оттайки
volatile unsigned long Mpause;        //минимальная пауза во включениях компрессора
volatile unsigned int DoorTimeLimit;  //пауза до сигнализации закрытия двери
volatile unsigned long oldFalling;    //закладка времени изменения спада
volatile unsigned long oldChange;     //закладка времени изменения
volatile byte TempLevel;              //фиксированная позиция терморегулятора 0...7
bool _motor;
bool  _heat;
bool startpause;  //активирована пауза при подаче питания
bool MinTemp;     //флаг означате что набрана минимальная установленная температура ХК
bool FirstCool;   //флаг означает что включение компрессора будет первым после разморозки
volatile bool _reqSaveData;
volatile bool _reqReadKey;
int cycleCounter;
volatile unsigned int TawCount;
volatile unsigned long TawLong;

void setup()
{
  pinMode(Motor, OUTPUT);
  pinMode(Heat, OUTPUT);
  pinMode(Lamp, OUTPUT);
  pinMode(LD0, OUTPUT);
  pinMode(LD1, OUTPUT);
  pinMode(LD2, OUTPUT);
  pinMode(ledPin, OUTPUT);
  pinMode(button, INPUT);
  pinMode(DoorSensor, INPUT);
  pinMode(A0, INPUT);
  pinMode(A1, INPUT);
  digitalWrite(A0, HIGH); //вкл подтягивающих резисторов
  digitalWrite(A1, HIGH);
  digitalWrite(button, HIGH); //вкл подтягивающих резисторов
  digitalWrite(DoorSensor, HIGH); //на кнопки
  _motor=false;
  _heat=false;
  FirstCool=false;
  startpause = true;
  TawLong = 360000; //время на оттайку Мор камеры =сек*100 (60 мин)
  Mpause=30000; //время паузы в работе компрессора =сек*100 (5 min)
  Timer1.initialize(1764);  //таймер выработки звуковых сигналов нота ДО;
  Timer1.pwm(Buzzer, 0);
  pinMode(Buzzer, OUTPUT);
  MsTimer2::set(10, Timer10); //таймер 10мсек
  MsTimer2::start();
  attachInterrupt(0, btnChange, CHANGE);
  _reqReadKey = false;
  TempLevel = EEPROM.read(0);   //считать настройку терморегулятора (0...7)
  TimeBase = EEPROM.get(1,TimeBase);  //считать сохраненное значение наработки мотора после размороки МК
  TawCount = EEPROM.get(5,TawCount);   //считать число оттаек МК
  Serial.begin(115200);
  TempH_1 = (unsigned long)analogRead(A0) << 7;
  TempM_1 =  (unsigned long)analogRead(A1) << 7; //чтение термодатчиков в регистр предыдущего состояния
}

void loop()
{
  int h, m, s;
  //пауза при включении питания чтобы датчики температуры усреднились
  if ((millis() < 10000) && (startpause)) return;
  startpause = false;
  // сигнализации
  if (_motor & _heat)  Timer1.setPwmDuty(Buzzer, 512); //сигнализация если одновременно и мотор и нагрев включились
  else if ((DoorTimeLimit == 0) & (millis() % 512 < 256)) Timer1.setPwmDuty(Buzzer, 512);
  else
    Timer1.setPwmDuty(Buzzer, 0);

  if (!digitalRead(DoorSensor)) //дверь открыта ?
    {
    digitalWrite(Lamp, HIGH);       //включить лампу
    CloseDoorTime=0;
    }
  else  //дверь закрыта
    {
    DoorTimeLimit = 2500;     //поддерживать таймер на 25сек
    digitalWrite(Lamp, LOW);  //и погасить лампу
    if (_reqSaveData)         //Есть запрос на сохранение параметров ?
      {
      EEPROM.write(0, TempLevel);
      _reqSaveData = false;
      }
    }

  //Управление выключением оттайки
  if (_heat & ((AverM < EndTawTemp) | (TawLong == 0))) //МК нагрелась свыше температуры оттайки или вышло время оттайки
    {
    _heat = false; //отключить нагрев
    FirstCool=true;
    Mpause=12000;   //пауза до включения компрессора (2мин)
    if (AverM <= EndTawTemp)
      {
      TawCount++;       //увеличить счетчик числа оттаек
      EEPROM.put(5, TawCount);
      TimeBase = tawTime; //период оттайки взвести
      EEPROM.put(1, TimeBase);
      Serial.println("Успешная оттайка. Наработка взведена и сохранена в EEPROM");
      }
    }
    
  //Управление компрессором  
  MinTemp=false;
  Ustavka = (unsigned long)TempLevel * 512 + 94488;
  if (((AverH < Ustavka) | FirstCool) & !_heat & !_motor & (Mpause==0)) _motor = true;               //условие включения компрессора
  if ((!FirstCool & (AverH > Ustavka + Hys) & _motor ) | (FirstCool & (AverM>RecoverTemp) & _motor))  //условие выключения компрессора
      {
      _motor = false;        //Выключить компрессор
      MinTemp=true;
      if (FirstCool) Mpause=90000;   //пауза до включения компрессора (15мин)
       else Mpause=30000;   //пауза до включения компрессора (5мин)
      EEPROM.put(1, TimeBase); 
      Serial.println("Наработка сохранена в EEPROM");
      FirstCool=false;
      }

  //Управление включением оттайки  
  if (!_heat & !_motor & (TimeBase == 0) & MinTemp & (CloseDoorTime>CDoorClose))     //компрессор выключен и подошло время оттайки ?
    {
    TawLong = 360000; // задать лимит длительности оттайки 60 min
    _heat = true;      //Включить нагрев
    }

  digitalWrite(Motor, _motor);
  digitalWrite(Heat, _heat);
  //индикация уставки на светодиодах
  if (digitalRead(button) == true)
    {
    digitalWrite(LD2, TempLevel & 1);
    digitalWrite(LD1, TempLevel & 2);
    digitalWrite(LD0, TempLevel & 4);
    }
  else  if (millis() - oldChange > 1000) //при нажатой кнопке индикация состояния
    {
    digitalWrite(LD0, _motor);
    digitalWrite(LD1, _heat);
    digitalWrite(LD2, (TimeBase == 0));
    }
  cycleCounter++;
  if (cycleCounter > 10000) //индикация на шину раз в 10000
    {
    Serial.print("  Уровнь="); Serial.print(TempLevel);
    Serial.print("  Тхк="); Serial.print(AverH);
    Serial.print("  Тмк="); Serial.print(AverM);
    h = TimeBase / 360000;
    m = (TimeBase % 360000) / 6000;
    s = (TimeBase % 6000) / 100;
    Serial.print("  до оттайки "); Serial.print(h); Serial.print("h "); Serial.print(m); Serial.print("m "); Serial.print(s); Serial.print("s ");
    Serial.print("  до конца оттайки "); Serial.print(TawLong);
    Serial.print("  Motor "); Serial.print(_motor); Serial.print("  Heat "); Serial.print(_heat); Serial.print("  Пределы: "); Serial.print(Ustavka);
    Serial.print("  ... "); Serial.print(Ustavka + Hys); Serial.print(" Число оттаек "); Serial.println(TawCount);
    cycleCounter = 0;
    }
}

void Timer10()
{
  if (_reqReadKey && (millis() - oldChange > 100))
    {
    _reqReadKey = false;
    if (digitalRead(button) == HIGH) //момент отжатия кнопки
      {
      if (millis() - oldFalling < 1000)
        {
        _reqSaveData = true;
        TempLevel++;
        if (TempLevel == 8) TempLevel = 0;
        }
      }
    else oldFalling = millis();   //момент нажатия кнопки
    }
  TempH = (unsigned long)analogRead(A0) << 7;
  TempM = (unsigned long)analogRead(A1) << 7; //чтение термодатчиков
  AverH = TempH_1 - (TempH_1 >> 7) + (TempH >> 7);
  AverM = TempM_1 - (TempM_1 >> 7) + (TempM >> 7); //экспотенциальный фильтр
  TempH_1 = AverH;
  TempM_1 = AverM;
  if (DoorTimeLimit > 0) DoorTimeLimit--;
  if ((TimeBase > 0) & _motor) TimeBase--; //считать моточасы работы компрессора
  if ((TawLong > 0) & _heat) TawLong--;
  if ((Mpause > 0) & ! _motor) Mpause--; //считать отдых компрессора
  CloseDoorTime++;

}

void btnChange()
{
  if (startpause) return;
  oldChange = millis();
  _reqReadKey = true;
}

