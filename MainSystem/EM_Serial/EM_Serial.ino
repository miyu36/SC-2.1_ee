#include <TinyGPS++.h>
#include <math.h>

TinyGPSPlus gps;

int phase = 4;
char key = '0';
const int SAMPLING_RATE = 200;
int phase_state = 0;

int launch_PIN = 32;            //トランジスタのピン番号の宣言
int launch_outputsecond = 5;       //点火時の9V電圧を流す時間，単位はsecond

int64_t sensorValue_bin[14];
int Datanumber = 0;

//phase3で使用する変数
int type = 1;
int yeah = 1;
int type_state = 0;
int cutparac = 33;                  //切り離し用トランジスタのピン番号の宣言
int outputcutsecond = 5;            //切り離し時の9V電圧を流す時間，単位はsecond
float time3_1,time3_2,St_Time;      //時間に関するもの
float Accel[6];                     //計測した値をおいておく関数
float Altitude[6];                  //(高度)
float Preac,differ1,Acsum,Acave,RealDiffer1;
float Preal,differ2,Alsum,Alave,RealDiffer2;
int i=0;
int j=0;

float RealDiffer;

//for MPU6050
#include <MPU9250_asukiaaa.h>
#ifdef _ESP32_HAL_I2C_H_
#define SDA_MPU 21 //I2C通信
#define SCL_MPU 22
#endif

MPU9250_asukiaaa mySensor;

//for BMP180
#include <Wire.h> //I2C通信
#include <Adafruit_BMP085.h>
Adafruit_BMP085 bmp;
#define SDA_BMP 21
#define SCL_BMP 22

//for SD Card
#include <SPI.h> //SDカードはSPI通信
#include <SD.h>

File CanSatLogData;
File SensorData;

const int sck=13 , miso=15 , mosi=14 , ss=27;

//センサー値の格納
double Temperature, Pressure, accelX, accelY, accelZ, magX, magY, magZ, gyroX, gyroY, gyroZ, accelSqrt = 0, gps_latitude, gps_longitude, gps_altitude, altitude = 0;
int gps_time;


//for phase1,2
int mode_average = 0;
int mode_comparison = 0;
int count1 = 0;
int count2 = 0;
int count3 = 0;
double ground = -89.0;
double altitude_average = 10000;
double altitude_sum = 0;
double TBD_altitude = 7; //終端速度3[m\s]*切断にかかる時間2[s]+パラシュートがcansatにかぶらないで分離できる高度1[m]
double alt[5];
unsigned long current_millis;
unsigned long previous_millis;
double altitude_sum_mpu = 0;
double altitude_sum_bmp = 0;
double altitude_sum_gps = 0;
double previous_altitude;
double current_altitude;

volatile int timeCounter1;
hw_timer_t *timer1 = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

// 64bit整数データのバイナリー変換関数
void casttobyte64(int64_t data, byte buf[]){
    buf[0] = (data >> 56) & 0x00FF;
    buf[1] = (data >> 48) & 0x00FF;
    buf[2] = (data >> 40) & 0x00FF;
    buf[3] = (data >> 32) & 0x00FF;
    buf[4] = (data >> 24) & 0x00FF;
    buf[5] = (data >> 16) & 0x00FF;
    buf[6] = (data >> 8) & 0x00FF;
    buf[7] = (data) & 0x00FF;
}


// 16bit整数データのバイナリー変換関数
void casttobyte16(int16_t data, byte buf[]){
    buf[0] = (data >> 8) & 0x00FF;
    buf[1] = (data) & 0x00FF;
}

// Interrupt timer function
void IRAM_ATTR onTimer1(){
    portENTER_CRITICAL_ISR(&timerMux);
    timeCounter1++;
    portEXIT_CRITICAL_ISR(&timerMux);
}



void setup() {
    Serial1.begin(9600, SERIAL_8N1, 5, 18);
    // SD Card initialization
    SPI.begin(sck,miso,mosi,ss);
    SD.begin(ss,SPI);
    CanSatLogData = SD.open("/CanSatLogData.log", FILE_APPEND);
    SensorData = SD.open("/SensorData.bin",FILE_APPEND);

    //割り込み関数
    timer1 = timerBegin(0, 80, true);
    timerAttachInterrupt(timer1, &onTimer1, true);
    timerAlarmWrite(timer1, 1.0E6 / SAMPLING_RATE, true);
    timerAlarmEnable(timer1);
    //無線通信
    Serial.begin(115200);
    //Serial.begin(115200, SERIAL_8N1, 16, 17); //関数内の引数はデータ通信レート，unknown，RXピン，TXピン
    Serial.write("TESTING: Serial communication\n");
    Serial.write("TESTING: Serial communication\n");

    //LED
    pinMode(launch_PIN, OUTPUT);        //点火用トランジスタの出力宣言
    pinMode(cutparac, OUTPUT);      //切り離し用トランジスタの出力宣言
    digitalWrite(launch_PIN, LOW);      //点火用トランジスタの出力オフ
    digitalWrite(cutparac, LOW);    //切り離し用トランジスタの出力オフ

    //for MPU6050
    Wire.begin(SDA_BMP, SCL_BMP);
    mySensor.beginAccel();
    mySensor.beginGyro();
    mySensor.beginMag();
    // You can set your own offset for mag values
    //Offset値を変える必要あり
    mySensor.magXOffset = -50;
    mySensor.magYOffset = -55;
    mySensor.magZOffset = -10;

    //for BMP
    bmp.begin();

    CanSatLogData.println("START RECORD");
    CanSatLogData.flush();

}//setup関数閉じ


void loop() {
 if (Serial1.available() > 0) {//これないとGPS使えない
   char c = Serial1.read();
   gps.encode(c);
   if (gps.location.isUpdated()) {//この中でgpsのデータを使わないと，うまく値が表示されない
    if (timeCounter1 > 0) {
        portENTER_CRITICAL(&timerMux);
        timeCounter1--;
        portEXIT_CRITICAL(&timerMux);

        //起動時刻の更新
        unsigned long currentMillis = millis();

        //センサー値のアップデート
        mySensor.accelUpdate();
        mySensor.gyroUpdate();
        mySensor.magUpdate();


        //センサー値取得
        Temperature = bmp.readTemperature();
        Pressure = bmp.readPressure();

        if (mySensor.accelUpdate() == 0) {
            accelX = mySensor.accelX();
            accelY = mySensor.accelY();
            accelZ = mySensor.accelZ();
            accelSqrt = mySensor.accelSqrt();
        }

        if (mySensor.magUpdate() == 0) {
            magX = mySensor.magX();
            magY = mySensor.magY();
            magZ = mySensor.magZ();
        }

        if (mySensor.gyroUpdate() == 0) {
            gyroX = mySensor.gyroX();
            gyroY = mySensor.gyroY();
            gyroZ = mySensor.gyroZ();
        }

        altitude = bmp.readAltitude();
        gps_latitude = gps.location.lat();
        gps_longitude = gps.location.lng();
        gps_time = gps.time.value();

        //各フェーズごとの記述
        switch (phase){
          
            //########## 待機フェーズ ##########
            case 1:

                if(phase_state != 1){
                    //待機フェーズに入ったとき１回だけ実行したいプログラムを書く
                    Serial.write("Phase1: transition completed\n");    // 地上局へのデータ送信

                    //LogDataの保存
                    CanSatLogData.println(gps_time);
                    CanSatLogData.println("Phase1: transition completed");    
                    CanSatLogData.flush();
                    
                    phase_state = 1;
                }          

                if(accelZ < -2){//落下開始をMPUで判定
                    altitude_sum_mpu += altitude;
                    count1++;
                    if(count1==1){
                        count1=0;
                        CanSatLogData.println(gps_time);
                        CanSatLogData.println("FALL STARTED(by MPU)\n");    
                        CanSatLogData.flush();
                        phase = 2;
                    }
                }

                switch(mode_comparison){//落下開始をBMPで判定
                    case 0:     
                        previous_millis = millis();
                        altitude_sum_bmp += altitude;
                        count3++;
                        if(count3==5){
                            previous_altitude = altitude_sum_bmp/5;
                            altitude_sum_bmp = 0;
                            count3 = 0;
                            mode_comparison = 1;
                        }
                        break;

                    case 1://500ms後     
                        current_millis = millis();
                        if(current_millis - previous_millis >= 500){
                            altitude_sum_bmp += altitude;
                            count3++;
                            if(count3==5){
                                current_altitude = altitude_sum_bmp/5;
                                CanSatLogData.println(currentMillis);
                                CanSatLogData.println("current_altitude - previous_altitude = \n");  
                                CanSatLogData.println(current_altitude - previous_altitude);  
                                if(current_altitude - previous_altitude <= -1.0){
                                    CanSatLogData.println("FALL STARTED(by BMP)\n");                                                      
                                    CanSatLogData.flush();
                                    phase = 2;
                                }else{
                                    altitude_sum_bmp = 0;
                                    count3 = 0;
                                    mode_comparison = 0;
                                } 
                            } 
                        }
                        break;
                }
                break;


            //########## 降下フェーズ ##########
            case 2:
                if(phase_state != 2){
                    //降下フェーズに入ったとき１回だけ実行したいプログラムを書く
                    Serial.write("Phase2: transition completed\n"); //地上局へのデータ送信
                    
                    //LogDataの保存
                    CanSatLogData.println(gps_time);
                    CanSatLogData.println("Phase2: transition completed");    
                    CanSatLogData.flush();
                    phase_state = 2;
                }

                if(altitude_average - ground>TBD_altitude){
                    if(mode_average==0){//5個のデータがたまるまで
                        alt[count1] = altitude;
                        count1++;
                        if(count1==5){
                            for(count2=0;count2<5;count2++){
                                altitude_sum_bmp += alt[count2]; // いったん受信したデータを足す
                            }
                            altitude_average = altitude_sum_bmp/5;
                            mode_average = 1;
                            count1=0;
                        }
                    }else{//5個のデータがたまった後
                        altitude_sum_bmp = 0;
                        altitude_average = 0;
                        for(count2=0;count2<4;count2++){
                            alt[count2]=alt[count2+1];
                        }
                        alt[4]=altitude;
                        for(count2=0;count2<5;count2++){
                            altitude_sum_bmp += alt[count2];
                        }
                        altitude_average = altitude_sum_bmp/5;
                    }
                }else{//ニクロム線に電流を流す高度以下になったら
                    phase = 3;
                }
            break;


            //########## 分離フェーズ ##########
            case 3:
                if(phase_state != 3){
                    //分離フェーズに入ったとき１回だけ実行したいプログラムを書く
                    Serial.write("Phase3: transition completed\n");

                    //LogDataの保存
                    CanSatLogData.println(gps_time);
                    CanSatLogData.println("Phase3: transition completed");    
                    CanSatLogData.flush();

                    phase_state = 3;
                    time3_1 = currentMillis;                           //phase3　開始時間の保存
                    St_Time = time3_1 + outputcutsecond * 1000;        //基準時間

                    Serial.write("WARNING: The cut-para code has been entered.\n");
                    digitalWrite(cutparac, HIGH); //オン
                    Serial.write("WARNING: 9v voltage is output.\n");

                    //LogDataの保存
                    CanSatLogData.println(currentMillis);
                    CanSatLogData.println("9v voltage is output");
                    CanSatLogData.flush();
                    
                }

                switch(type){
                    case 1:
                        if(type_state != 1){     //電流フェーズに入ったとき１回だけ実行したいプログラムを書く
                            Serial.write("Phase3_type1: transition completed\n");
                            Serial.write("");
                            type_state = 1;
                        }

                        if(currentMillis > St_Time){     //電流を流した時間が基準時間を超えたら
                            digitalWrite(cutparac, LOW); //オフ
                            Serial.write("WARNING: 9v voltage is stop.\n");
                            CanSatLogData.println(currentMillis);
                            CanSatLogData.println("WARNING: 9v voltage is stop.\n");
                            CanSatLogData.flush();
                            type = 2;
                        }
                        break;


                case 2:  //type = 2
                    
                        if(type_state != 2){  //停止フェーズに入ったとき１回だけ実行したいプログラムを書く
                            Serial.write("Phase3_type2: transition completed\n");
                            Serial.write("");
                            CanSatLogData.println(currentMillis);
                            CanSatLogData.println("Phase3_type2: transition completed\n");
                            CanSatLogData.flush();
                            type_state = 2;
                            i = 0;
                            j = 0;
                            Preac = 0;         //1個前の加速度を記憶
                            Preal = 0;
                            differ1 = 0.1;     //加速度　移動平均の差
                            differ2 = 0.5;     //高度　　移動平均の差
                            Acave = 0;         //加速度　5個の平均値
                            Alave = 0;         //高度　　5個の平均値
                            Acsum = 0;         //加速度　5個の合計値
                            Alsum = 0;         //高度　　5個の合計値
                            RealDiffer1 = 0;   //1秒前との差を記憶する
                            RealDiffer2 = 0;
                        }

                        if(yeah == 1){     //データを初めから五個得るまで
                            Accel[i] = accelSqrt;
                            Altitude[i] = altitude;
                            i = i + 1;
                            if(i == 6){        //5個得たらその時点での平均値を出し，次のフェーズへ
                                yeah = 2;
                                i = 0; //iの値をリセット
                                for(j=1 ; j<6 ; j++){   //j=0の値は非常に誤差が大きいので1から
                                    Acsum = Acsum + Accel[j];  
                                    Alsum = Alsum + Altitude[j];
                                }
                                Acave = Acsum/5;
                                Alave = Alave/5;
                                time3_2 = currentMillis;
                            }
                        }else{
                            Preac = Acave;
                            Preal = Alave;  
                            Accel[i] = accelSqrt;
                            Altitude[i] = altitude;
                            for(j=0 ; j<5 ; j++){
                                Acsum = Acsum + Accel[j];
                                Alsum = Alsum + Altitude[j];
                            }
                            Acave = Acsum/5;
                            Alave = Alsum/5;
                            RealDiffer1 = Preac - Acave;
                            RealDiffer2 = Preal - Alave;
                            if(i == 5){
                                i = 0;
                                Acsum = 0; 
                                Alsum = 0;
                            }else{
                                i = i+1;
                                Acsum = 0; 
                                Alsum = 0;
                            }
                            if(currentMillis - time3_2 > 1000){
                                if( RealDiffer1 < differ1 ){ //移動平均が基準以内の変化量だった時
                                    phase = 4;
                                }else if( RealDiffer2 < differ2 ){
                                    phase = 4;
                                }else{
                                    time3_2 = currentMillis;
                                }
                            }
                        }
                        break;
                }
                break;

            //########## 遠距離探索フェーズ ##########
            case 4:
                if(phase_state != 4){
                    //遠距離探索フェーズに入ったとき１回だけ実行したいプログラムを書く
                    Serial.write("Phase4: transition completed\n");//地上局へのデータ

                    //LogDataの保存
                    CanSatLogData.println(gps_time);
                    CanSatLogData.println("Phase4: transition completed");    
                    CanSatLogData.flush();
                    
                    phase_state = 4;
                }




                

            //########## 近距離探索フェーズ ##########
            case 5:
                if(phase_state != 5){
                    //近距離探索フェーズに入ったとき１回だけ実行したいプログラムを書く
                    Serial.write("Phase5: transition completed\n"); // 地上局へのデータ

                    //LogDataの保存
                    CanSatLogData.println(gps_time);
                    CanSatLogData.println("Phase5: transition completed");    
                    CanSatLogData.flush();

                    phase_state = 5;
                }










                
        }
    
        Datanumber++;
        //SDカードへデータを保存する
        sensorValue_bin[0] = Temperature * 1000;
        sensorValue_bin[1] = Pressure * 1000;
        sensorValue_bin[2] = accelX * 1000;
        sensorValue_bin[3] = accelY * 1000;
        sensorValue_bin[4] = accelZ * 1000;
        sensorValue_bin[5] = magX * 1000;
        sensorValue_bin[6] = magY * 1000;
        sensorValue_bin[7] = magZ * 1000;
        sensorValue_bin[8] = gyroX * 1000;
        sensorValue_bin[9] = gyroY * 1000;
        sensorValue_bin[10] = gyroZ * 1000;
        sensorValue_bin[11] = gps_latitude * 1000000000;
        sensorValue_bin[12] = gps_longitude * 1000000000;
        sensorValue_bin[13] = gps_time;
        
        for (int i = 0; i<14; i++) {
                byte buf[8];
                casttobyte64(sensorValue_bin[i],buf);
                SensorData.write(buf,sizeof(buf));
        }

        if (Datanumber%200 == 0){
            SensorData.flush();
        }
    }
   }
 }
}//loop関数の閉じ