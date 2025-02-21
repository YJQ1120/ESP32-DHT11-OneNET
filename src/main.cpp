/*
 * File: src/main.cpp
 * PlatformIO项目完整代码（适配OneNet）
《基于ESP32的室内温湿度监测系统》
 * 功能：DHT11温湿度数据采集 → OneNet云端上传
 */
#include <Arduino.h>
#include <WiFi.h>
#include <PubSubClient.h>
#include <Ticker.h>
#include <ArduinoJson.h>
#include <DHT.h>
#define LED 2 // LED灯连接到GPIO2,用LED灯指示设备状态

#define DHTPIN 4     // DHT11传感器数据引脚连接到GPIO4
#define DHTTYPE DHT11// DHT11传感器类型
#define product_id " "	// 产品ID
#define device_id " "// 设备ID
#define token " " // token

const char *ssid = " ";		 // WiFi名称
const char *password = " "; // WiFi密码

const char *mqtt_server = "mqtts.heclouds.com"; // MQTT服务器地址
const int mqtt_port = 1883;						// MQTT服务器端口

#define ONENET_TOPIC_PROP_POST "$sys/" product_id "/" device_id "/thing/property/post"
// 设备属性上报请求,设备---->OneNET
#define ONENET_TOPIC_PROP_SET "$sys/" product_id "/" device_id "/thing/property/set"
// 设备属性设置请求,OneNET---->设备
#define ONENET_TOPIC_PROP_POST_REPLY "$sys/" product_id "/" device_id "/thing/property/post/reply"
// 设备属性上报响应,OneNET---->设备
#define ONENET_TOPIC_PROP_SET_REPLY "$sys/" product_id "/" device_id "/thing/property/set_reply"
// 设备属性设置响应,设备---->OneNET
#define ONENET_TOPIC_PROP_FORMAT "{\"id\":\"%u\",\"version\":\"1.0\",\"params\":%s}"
// 设备属性格式模板
int postMsgId = 0; // 消息ID,消息ID是需要改变的,每次上报属性时递增

//=============全局变量定义================
bool LED_Status = false;		// LED状态
float temp = 0.0;				// 温度
float humi = 0.0;				// 湿度
WiFiClient espClient;			// 创建一个WiFiClient对象
PubSubClient client(espClient); // 创建一个PubSubClient对象
Ticker ticker;					// 创建一个定时器对象
DHT dht(DHTPIN, DHTTYPE);		// DHT11传感器对象

//=============功能函数================
void LED_Flash(int time);
void WiFi_Connect();// 连接WiFi
void OneNet_Connect();// 连接OneNet
void OneNet_Prop_Post();// 上报设备属性
void sendSensorData();// 发送传感器数据
void callback(char *topic, byte *payload, unsigned int length); // 回调函数

// =================================================================
//                           初始化设置
// =================================================================
void setup()
{
	pinMode(LED, OUTPUT);				 // LED灯设置为输出模式
	digitalWrite(LED, LOW);				 // 初始化 LED 为关闭状态
	LED_Status = false;					 // 初始化 LED_Status 为 false
	Serial.begin(115200);				 // 串口初始化,波特率115200,用于输出调试信息，这里串口波特率要与串口监视器设置的一样，否则会乱码
	dht.begin();						 // DHT11传感器初始化
	WiFi_Connect();						 // 连接WiFi
	OneNet_Connect();					 // 连接OneNet
	ticker.attach(10, OneNet_Prop_Post); // 定时器,每10s执行一次OneNet_Prop_Post函数
}

// =================================================================
//                           主循环
// =================================================================
void loop()
{
	if (WiFi.status() != WL_CONNECTED)
	{					// 如果WiFi连接断开
		WiFi_Connect(); // 重新连接WiFi
	}
	if (!client.connected())
	{					  // 如果MQTT连接断开
		OneNet_Connect(); // 重新连接OneNet
	}
	client.loop();	  // 保持MQTT连接
	sendSensorData(); // 发送传感器数据
}

//===============================================================
//                     LED闪烁函数
//===============================================================
void LED_Flash(int time)
{
	digitalWrite(LED, HIGH); // 点亮LED
	delay(time);			 // 延时time
	digitalWrite(LED, LOW);	 // 熄灭LED
	delay(time);			 // 延时time
}

//===============================================================
//					 WiFi连接函数
//===============================================================
void WiFi_Connect()
{
	WiFi.begin(ssid, password); // 连接WiFi
	while (WiFi.status() != WL_CONNECTED)
	{											// 等待WiFi连接,WiFI.status()返回当前WiFi连接状态,WL_CONNECTED为连接成功状态
		LED_Flash(500);							// LED闪烁,循环等待
		Serial.println("\n等待连接到WiFi网络"); // 输出信息
	}
	Serial.println("WiFi连接成功"); // WiFi连接成功
	Serial.println(WiFi.localIP()); // 输出设备IP地址
	digitalWrite(LED, HIGH);		// 点亮LED,表示WiFi连接成功
}

//===============================================================
//					 OneNet连接函数
//===============================================================
void OneNet_Connect()
{
	client.setServer(mqtt_server, mqtt_port);	  // 设置MQTT服务器地址和端口
	client.connect(device_id, product_id, token); // 连接OneNet
	if (client.connected())						  // 如果连接成功
	{
		LED_Flash(500);
		Serial.println("OneNet连接成功!");
	}
	else
	{
		Serial.println("OneNet连接失败!");
		Serial.println("错误代码: " + String(client.state()));
	}
	client.subscribe(ONENET_TOPIC_PROP_SET);		// 订阅设备属性设置请求, OneNET---->设备
	client.subscribe(ONENET_TOPIC_PROP_POST_REPLY); // 订阅设备属性上报响应,OneNET---->设备
}

//===============================================================
//					 传感器数据发送函数
//===============================================================
void sendSensorData()
{
	// 读取数据
	float newTemp = dht.readTemperature();
	float newHumi = dht.readHumidity();
	// 数据校验
	if (isnan(newTemp) || isnan(newHumi))
	{
		Serial.println("[传感器DHT11] 数据读取失败!");
		return;
	}
	temp = newTemp; // 更新温湿度数据
	humi = newHumi; // 更新温湿度数据
}
//===============================================================
//					 OneNet属性上报函数
//===============================================================
void OneNet_Prop_Post()
{
	if (client.connected())
	{
		char parmas[256];																																					 // 属性参数
		char jsonBuf[256];																																					 // JSON格式数据,用于上报属性的缓冲区
		sprintf(parmas, "{\"CurrentTemperature\":{\"value\":%.1f},\"CurrentHumidity\":{\"value\":%.1f},\"LED\":{\"value\":%s}}", temp, humi, LED_Status ? "true" : "false"); // 设置属性参数
		Serial.println(parmas);
		sprintf(jsonBuf, ONENET_TOPIC_PROP_FORMAT, postMsgId++, parmas); // 设置JSON格式数据,包括消息ID和属性参数
		Serial.println(jsonBuf);
		if (client.publish(ONENET_TOPIC_PROP_POST, jsonBuf)) // 上报属性
		{
			LED_Flash(500);
			Serial.println("数据发送成功!");
		}
		else
		{
			Serial.println("数据发送失败!");
		}
	}
}

//===============================================================
//					 MQTT回调函数
//===============================================================
void callback(char *topic, byte *payload, unsigned int length)
{
	Serial.print("收到消息，主题: ");
	Serial.print(topic);
	Serial.print("，内容: ");
	for (int i = 0; i < length; i++) // 遍历打印消息内容
	{
		Serial.print((char)payload[i]); // 打印消息内容
	}
	Serial.println();

	if (strcmp(topic, ONENET_TOPIC_PROP_SET) == 0) // 如果收到设备属性设置请求
	{
		StaticJsonDocument<256> doc;	 // 创建JSON文档
		char data[256];					 // 数据缓冲区
		for (int i = 0; i < length; i++) // 将消息内容复制到缓冲区
		{
			data[i] = (char)payload[i];
		}
		data[length] = '\0';											 // 添加字符串结束符
		DeserializationError error = deserializeJson(doc, data, length); // 解析JSON数据
		if (error)														 // 如果解析失败
		{
			Serial.println("JSON解析失败!");
			return;
		}
		if (doc.containsKey("LED")) // 如果收到LED控制命令
		{
			bool newLEDStatus = doc["LED"]["value"];									// 获取LED状态
			Serial.println("收到 LED 控制命令: " + String(newLEDStatus ? "开" : "关")); // 打印LED控制命令
			if (newLEDStatus != LED_Status)												// 如果LED状态发生改变
			{
				LED_Status = newLEDStatus;											   // 更新LED状态
				digitalWrite(LED, LED_Status ? HIGH : LOW);							   // 更新LED状态
				Serial.println("LED 状态已更新: " + String(LED_Status ? "开" : "关")); // 打印LED状态
			}
		}
	}
}
